// LevelDB Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//
// See port_example.h for documentation for the following types/functions.

// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
// 
//  * Redistributions of source code must retain the above copyright
//    notice, this list of conditions and the following disclaimer.
//  * Redistributions in binary form must reproduce the above copyright
//    notice, this list of conditions and the following disclaimer in the
//    documentation and/or other materials provided with the distribution.
//  * Neither the name of the University of California, Berkeley nor the
//    names of its contributors may be used to endorse or promote products
//    derived from this software without specific prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE REGENTS AND CONTRIBUTORS BE LIABLE FOR ANY
// DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
// ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//

#include "port/port_win.h"
#include "util/mutexlock.h"

#include <stack>
#include <cassert>

namespace leveldb {
    namespace port {
        //----------------cpp-----------------------------------

        void InitOnce(OnceType* once, void(*initializer)()) {

            if (InterlockedCompareExchange(once, 1, 0) == 0) {
                initializer();
            }
        }

        // MutexUnlock is a helper that will Release() the |lock| argument in the
        // constructor, and re-Acquire() it in the destructor.
        class MutexUnlock {
        public:
            explicit MutexUnlock(Mutex *mu) : mu_(mu) {
                // We require our caller to have the lock.
                mu_->AssertHeld();
                mu_->Unlock();
            }

            ~MutexUnlock() {
                mu_->Lock();
            }

        private:
            Mutex *const mu_;
            // No copying allowed
            MutexUnlock(const MutexUnlock&);
            void operator=(const MutexUnlock&);
        };

        CondVar::CondVar(Mutex* user_lock)
            : user_lock_(*user_lock),
            run_state_(RUNNING),
            allocation_counter_(0),
            recycling_list_size_(0) {
        }

        CondVar::~CondVar() {
            MutexLock auto_lock(&internal_lock_);
            run_state_ = SHUTDOWN;  // Prevent any more waiting.
            if (recycling_list_size_ != allocation_counter_) {  // Rare shutdown problem.
                // There are threads of execution still in this->TimedWait() and yet the
                // caller has instigated the destruction of this instance :-/.
                // A common reason for such "overly hasty" destruction is that the caller
                // was not willing to wait for all the threads to terminate.  Such hasty
                // actions are a violation of our usage contract, but we'll give the
                // waiting thread(s) one last chance to exit gracefully (prior to our
                // destruction).
                // Note: waiting_list_ *might* be empty, but recycling is still pending.
                MutexUnlock auto_unlock(&internal_lock_);
                SignalAll();  // Make sure all waiting threads have been signaled.
                Sleep(10);  // Give threads a chance to grab internal_lock_.
                // All contained threads should be blocked on user_lock_ by now :-).
            }  // Reacquire internal_lock_.
        }

        void CondVar::TimedWait(const int64_t max_time_in_ms) {
            Event* waiting_event;
            HANDLE handle;
            {
                MutexLock auto_lock(&internal_lock_);
                if (RUNNING != run_state_) return;  // Destruction in progress.
                waiting_event = GetEventForWaiting();
                handle = waiting_event->handle();
                assert(handle);
            }  // Release internal_lock.

  {
      MutexUnlock unlock(&user_lock_);  // Release caller's lock
      WaitForSingleObject(handle, static_cast<DWORD>(max_time_in_ms));
      // Minimize spurious signal creation window by recycling asap.
      MutexLock auto_lock(&internal_lock_);
      RecycleEvent(waiting_event);
      // Release internal_lock_
  }  // Reacquire callers lock to depth at entry.
        }

        // SignalAll() is guaranteed to signal all threads that were waiting (i.e., had
        // a cv_event internally allocated for them) before SignalAll() was called.
        void CondVar::SignalAll() {
            std::stack<HANDLE> handles;  // See FAQ-question-10.
            {
                MutexLock auto_lock(&internal_lock_);
                if (waiting_list_.IsEmpty())
                    return;
                while (!waiting_list_.IsEmpty())
                    // This is not a leak from waiting_list_.  See FAQ-question 12.
                    handles.push(waiting_list_.PopBack()->handle());
            }  // Release internal_lock_.
            while (!handles.empty()) {
                SetEvent(handles.top());
                handles.pop();
            }
        }

        // Signal() will select one of the waiting threads, and signal it (signal its
        // cv_event).  For better performance we signal the thread that went to sleep
        // most recently (LIFO).  If we want fairness, then we wake the thread that has
        // been sleeping the longest (FIFO).
        void CondVar::Signal() {
            HANDLE handle;
            {
                MutexLock auto_lock(&internal_lock_);
                if (waiting_list_.IsEmpty())
                    return;  // No one to signal.
                // Only performance option should be used.
                // This is not a leak from waiting_list.  See FAQ-question 12.
                handle = waiting_list_.PopBack()->handle();  // LIFO.
            }  // Release internal_lock_.
            SetEvent(handle);
        }

        // GetEventForWaiting() provides a unique cv_event for any caller that needs to
        // wait.  This means that (worst case) we may over time create as many cv_event
        // objects as there are threads simultaneously using this instance's Wait()
        // functionality.
        CondVar::Event* CondVar::GetEventForWaiting() {
            // We hold internal_lock, courtesy of Wait().
            Event* cv_event;
            if (0 == recycling_list_size_) {
                assert(recycling_list_.IsEmpty());
                cv_event = new Event();
                cv_event->InitListElement();
                allocation_counter_++;
                assert(cv_event->handle());
            }
            else {
                cv_event = recycling_list_.PopFront();
                recycling_list_size_--;
            }
            waiting_list_.PushBack(cv_event);
            return cv_event;
        }

        // RecycleEvent() takes a cv_event that was previously used for Wait()ing, and
        // recycles it for use in future Wait() calls for this or other threads.
        // Note that there is a tiny chance that the cv_event is still signaled when we
        // obtain it, and that can cause spurious signals (if/when we re-use the
        // cv_event), but such is quite rare (see FAQ-question-5).
        void CondVar::RecycleEvent(Event* used_event) {
            // We hold internal_lock, courtesy of Wait().
            // If the cv_event timed out, then it is necessary to remove it from
            // waiting_list_.  If it was selected by SignalAll() or Signal(), then it is
            // already gone.
            used_event->Extract();  // Possibly redundant
            recycling_list_.PushBack(used_event);
            recycling_list_size_++;
        }
        //------------------------------------------------------------------------------
        // The next section provides the implementation for the private Event class.
        //------------------------------------------------------------------------------

        // Event provides a doubly-linked-list of events for use exclusively by the
        // CondVar class.

        // This custom container was crafted because no simple combination of STL
        // classes appeared to support the functionality required.  The specific
        // unusual requirement for a linked-list-class is support for the Extract()
        // method, which can remove an element from a list, potentially for insertion
        // into a second list.  Most critically, the Extract() method is idempotent,
        // turning the indicated element into an extracted singleton whether it was
        // contained in a list or not.  This functionality allows one (or more) of
        // threads to do the extraction.  The iterator that identifies this extractable
        // element (in this case, a pointer to the list element) can be used after
        // arbitrary manipulation of the (possibly) enclosing list container.  In
        // general, STL containers do not provide iterators that can be used across
        // modifications (insertions/extractions) of the enclosing containers, and
        // certainly don't provide iterators that can be used if the identified
        // element is *deleted* (removed) from the container.

        // It is possible to use multiple redundant containers, such as an STL list,
        // and an STL map, to achieve similar container semantics.  This container has
        // only O(1) methods, while the corresponding (multiple) STL container approach
        // would have more complex O(log(N)) methods (yeah... N isn't that large).
        // Multiple containers also makes correctness more difficult to assert, as
        // data is redundantly stored and maintained, which is generally evil.

        CondVar::Event::Event() : handle_(0) {
            next_ = prev_ = this;  // Self referencing circular.
        }

        CondVar::Event::~Event() {
            if (0 == handle_) {
                // This is the list holder
                while (!IsEmpty()) {
                    Event* cv_event = PopFront();
                    assert(cv_event->ValidateAsItem());
                    delete cv_event;
                }
            }
            assert(IsSingleton());
            if (0 != handle_) {
                int ret_val = CloseHandle(handle_);
                assert(ret_val);
            }
        }

        // Change a container instance permanently into an element of a list.
        void CondVar::Event::InitListElement() {
            assert(!handle_);
            handle_ = CreateEvent(NULL, false, false, NULL);
            assert(handle_);
        }

        // Methods for use on lists.
        bool CondVar::Event::IsEmpty() const {
            assert(ValidateAsList());
            return IsSingleton();
        }

        void CondVar::Event::PushBack(Event* other) {
            assert(ValidateAsList());
            assert(other->ValidateAsItem());
            assert(other->IsSingleton());
            // Prepare other for insertion.
            other->prev_ = prev_;
            other->next_ = this;
            // Cut into list.
            prev_->next_ = other;
            prev_ = other;
            assert(ValidateAsDistinct(other));
        }

        CondVar::Event* CondVar::Event::PopFront() {
            assert(ValidateAsList());
            assert(!IsSingleton());
            return next_->Extract();
        }

        CondVar::Event* CondVar::Event::PopBack() {
            assert(ValidateAsList());
            assert(!IsSingleton());
            return prev_->Extract();
        }

        // Methods for use on list elements.
        // Accessor method.
        HANDLE CondVar::Event::handle() const {
            assert(ValidateAsItem());
            return handle_;
        }

        // Pull an element from a list (if it's in one).
        CondVar::Event* CondVar::Event::Extract() {
            assert(ValidateAsItem());
            if (!IsSingleton()) {
                // Stitch neighbors together.
                next_->prev_ = prev_;
                prev_->next_ = next_;
                // Make extractee into a singleton.
                prev_ = next_ = this;
            }
            assert(IsSingleton());
            return this;
        }

        // Method for use on a list element or on a list.
        bool CondVar::Event::IsSingleton() const {
            assert(ValidateLinks());
            return next_ == this;
        }

        // Provide pre/post conditions to validate correct manipulations.
        bool CondVar::Event::ValidateAsDistinct(Event* other) const {
            return ValidateLinks() && other->ValidateLinks() && (this != other);
        }

        bool CondVar::Event::ValidateAsItem() const {
            return (0 != handle_) && ValidateLinks();
        }

        bool CondVar::Event::ValidateAsList() const {
            return (0 == handle_) && ValidateLinks();
        }

        bool CondVar::Event::ValidateLinks() const {
            // Make sure both of our neighbors have links that point back to us.
            // We don't do the O(n) check and traverse the whole loop, and instead only
            // do a local check to (and returning from) our immediate neighbors.
            return (next_->prev_ == this) && (prev_->next_ == this);
        }


        /*
        FAQ On subtle implementation details:

        1) What makes this problem subtle?  Please take a look at "Strategies
        for Implementing POSIX Condition Variables on Win32" by Douglas
        C. Schmidt and Irfan Pyarali.
        http://www.cs.wustl.edu/~schmidt/win32-cv-1.html It includes
        discussions of numerous flawed strategies for implementing this
        functionality.  I'm not convinced that even the final proposed
        implementation has semantics that are as nice as this implementation
        (especially with regard to SignalAll() and the impact on threads that
        try to Wait() after a SignalAll() has been called, but before all the
        original waiting threads have been signaled).

        2) Why can't you use a single wait_event for all threads that call
        Wait()?  See FAQ-question-1, or consider the following: If a single
        event were used, then numerous threads calling Wait() could release
        their cs locks, and be preempted just before calling
        WaitForSingleObject().  If a call to SignalAll() was then presented on
        a second thread, it would be impossible to actually signal all
        waiting(?) threads.  Some number of SetEvent() calls *could* be made,
        but there could be no guarantee that those led to to more than one
        signaled thread (SetEvent()'s may be discarded after the first!), and
        there could be no guarantee that the SetEvent() calls didn't just
        awaken "other" threads that hadn't even started waiting yet (oops).
        Without any limit on the number of requisite SetEvent() calls, the
        system would be forced to do many such calls, allowing many new waits
        to receive spurious signals.

        3) How does this implementation cause spurious signal events?  The
        cause in this implementation involves a race between a signal via
        time-out and a signal via Signal() or SignalAll().  The series of
        actions leading to this are:

        a) Timer fires, and a waiting thread exits the line of code:

        WaitForSingleObject(waiting_event, max_time.InMilliseconds());

        b) That thread (in (a)) is randomly pre-empted after the above line,
        leaving the waiting_event reset (unsignaled) and still in the
        waiting_list_.

        c) A call to Signal() (or SignalAll()) on a second thread proceeds, and
        selects the waiting cv_event (identified in step (b)) as the event to revive
        via a call to SetEvent().

        d) The Signal() method (step c) calls SetEvent() on waiting_event (step b).

        e) The waiting cv_event (step b) is now signaled, but no thread is
        waiting on it.

        f) When that waiting_event (step b) is reused, it will immediately
        be signaled (spuriously).


        4) Why do you recycle events, and cause spurious signals?  First off,
        the spurious events are very rare.  They can only (I think) appear
        when the race described in FAQ-question-3 takes place.  This should be
        very rare.  Most(?)  uses will involve only timer expiration, or only
        Signal/SignalAll() actions.  When both are used, it will be rare that
        the race will appear, and it would require MANY Wait() and signaling
        activities.  If this implementation did not recycle events, then it
        would have to create and destroy events for every call to Wait().
        That allocation/deallocation and associated construction/destruction
        would be costly (per wait), and would only be a rare benefit (when the
        race was "lost" and a spurious signal took place). That would be bad
        (IMO) optimization trade-off.  Finally, such spurious events are
        allowed by the specification of condition variables (such as
        implemented in Vista), and hence it is better if any user accommodates
        such spurious events (see usage note in condition_variable.h).

        5) Why don't you reset events when you are about to recycle them, or
        about to reuse them, so that the spurious signals don't take place?
        The thread described in FAQ-question-3 step c may be pre-empted for an
        arbitrary length of time before proceeding to step d.  As a result,
        the wait_event may actually be re-used *before* step (e) is reached.
        As a result, calling reset would not help significantly.

        6) How is it that the callers lock is released atomically with the
        entry into a wait state?  We commit to the wait activity when we
        allocate the wait_event for use in a given call to Wait().  This
        allocation takes place before the caller's lock is released (and
        actually before our internal_lock_ is released).  That allocation is
        the defining moment when "the wait state has been entered," as that
        thread *can* now be signaled by a call to SignalAll() or Signal().
        Hence we actually "commit to wait" before releasing the lock, making
        the pair effectively atomic.

        8) Why do you need to lock your data structures during waiting, as the
        caller is already in possession of a lock?  We need to Acquire() and
        Release() our internal lock during Signal() and SignalAll().  If we tried
        to use a callers lock for this purpose, we might conflict with their
        external use of the lock.  For example, the caller may use to consistently
        hold a lock on one thread while calling Signal() on another, and that would
        block Signal().

        9) Couldn't a more efficient implementation be provided if you
        preclude using more than one external lock in conjunction with a
        single ConditionVariable instance?  Yes, at least it could be viewed
        as a simpler API (since you don't have to reiterate the lock argument
        in each Wait() call).  One of the constructors now takes a specific
        lock as an argument, and a there are corresponding Wait() calls that
        don't specify a lock now.  It turns that the resulting implmentation
        can't be made more efficient, as the internal lock needs to be used by
        Signal() and SignalAll(), to access internal data structures.  As a
        result, I was not able to utilize the user supplied lock (which is
        being used by the user elsewhere presumably) to protect the private
        member access.

        9) Since you have a second lock, how can be be sure that there is no
        possible deadlock scenario?  Our internal_lock_ is always the last
        lock acquired, and the first one released, and hence a deadlock (due
        to critical section problems) is impossible as a consequence of our
        lock.

        10) When doing a SignalAll(), why did you copy all the events into
        an STL queue, rather than making a linked-loop, and iterating over it?
        The iterating during SignalAll() is done so outside the protection
        of the internal lock. As a result, other threads, such as the thread
        wherein a related event is waiting, could asynchronously manipulate
        the links around a cv_event.  As a result, the link structure cannot
        be used outside a lock.  SignalAll() could iterate over waiting
        events by cycling in-and-out of the protection of the internal_lock,
        but that appears more expensive than copying the list into an STL
        stack.

        11) Why did the lock.h file need to be modified so much for this
        change?  Central to a Condition Variable is the atomic release of a
        lock during a Wait().  This places Wait() functionality exactly
        mid-way between the two classes, Lock and Condition Variable.  Given
        that there can be nested Acquire()'s of locks, and Wait() had to
        Release() completely a held lock, it was necessary to augment the Lock
        class with a recursion counter. Even more subtle is the fact that the
        recursion counter (in a Lock) must be protected, as many threads can
        access it asynchronously.  As a positive fallout of this, there are
        now some DCHECKS to be sure no one Release()s a Lock more than they
        Acquire()ed it, and there is ifdef'ed functionality that can detect
        nested locks (legal under windows, but not under Posix).

        12) Why is it that the cv_events removed from list in SignalAll() and Signal()
        are not leaked?  How are they recovered??  The cv_events that appear to leak are
        taken from the waiting_list_.  For each element in that list, there is currently
        a thread in or around the WaitForSingleObject() call of Wait(), and those
        threads have references to these otherwise leaked events. They are passed as
        arguments to be recycled just aftre returning from WaitForSingleObject().

        13) Why did you use a custom container class (the linked list), when STL has
        perfectly good containers, such as an STL list?  The STL list, as with any
        container, does not guarantee the utility of an iterator across manipulation
        (such as insertions and deletions) of the underlying container.  The custom
        double-linked-list container provided that assurance.  I don't believe any
        combination of STL containers provided the services that were needed at the same
        O(1) efficiency as the custom linked list.  The unusual requirement
        for the container class is that a reference to an item within a container (an
        iterator) needed to be maintained across an arbitrary manipulation of the
        container.  This requirement exposes itself in the Wait() method, where a
        waiting_event must be selected prior to the WaitForSingleObject(), and then it
        must be used as part of recycling to remove the related instance from the
        waiting_list.  A hash table (STL map) could be used, but I was embarrased to
        use a complex and relatively low efficiency container when a doubly linked list
        provided O(1) performance in all required operations.  Since other operations
        to provide performance-and/or-fairness required queue (FIFO) and list (LIFO)
        containers, I would also have needed to use an STL list/queue as well as an STL
        map.  In the end I decided it would be "fun" to just do it right, and I
        put so many assertions (DCHECKs) into the container class that it is trivial to
        code review and validate its correctness.

        */

    }
}
