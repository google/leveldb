
import sys
import re

msg = "Parsing mem file..."
filename = "mem.txt"
filepath = sys.argv[1]
cpufile = filepath + filename
csvname = "mem.csv"
csvfile = filepath + csvname
print(msg)

rf = open(cpufile, 'r')
wf = open(csvfile, 'w')
Lines = rf.readlines()

count = 0
for line in Lines:
    count += 1
    if line != '\n':
        ind = re.sub(' +', ' ', line).split(' ')
        if ind[0] != 'Linux':
            if ind[0] != '平均时间:':
                if ind[2] == 'kbmemfree':
                    wf.write("time" + "," + ind[2] + "," + ind[3] + "," + ind[4] + "," + ind[5] + "," + ind[6] + "," + ind[7]+ "," + ind[8]+ "," + ind[12])
                        # print("time," + ind[3] + "," + ind[5] + "," + ind[6] + "," + ind[12])
                else:
                    wf.write(ind[0] + ind[1] + "," + ind[2] + "," + ind[3] + "," + ind[4] + "," + ind[5] + "," + ind[6]+ "," + ind[7]+ "," + ind[8]+ "," + ind[12])
                        # print(ind[0] + ind[1] + "," + ind[3] + "," + ind[5] + "," + ind[6] + "," + ind[12])
rf.close()
wf.close()