
import sys
import re

msg = "Parsing CPU file..."
filename = "cpu.txt"
filepath = sys.argv[1]

cpufile = filepath + filename
csvname = "cpu.csv"
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
                if ind[2] == 'CPU':
                    wf.write("time," + ind[3] + "," + ind[5] + "," + ind[6] + "," + ind[12])
                    #print("time," + ind[3] + "," + ind[5] + "," + ind[6] + "," + ind[12])
                else:
                    wf.write(ind[0] + ind[1] + "," + ind[3] + "," + ind[5] + "," + ind[6] + "," + ind[12])
                    #print(ind[0] + ind[1] + "," + ind[3] + "," + ind[5] + "," + ind[6] + "," + ind[12])
rf.close()
wf.close()