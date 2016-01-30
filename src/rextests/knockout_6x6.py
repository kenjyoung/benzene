import time
from program import Program

PLAYERS = {0: "white", 1: "black"}
PASSCOLOR = '\033[92m'
FAILCOLOR = '\033[91m'
ENDCOLOR = '\033[0m'

mohex = Program("../mohex/./mohex 2>/dev/null", False)
mohex.sendCommand("param_dfpn threads 4")
mohex.sendCommand("boardsize 6")

mohex.sendCommand("play white d2")
start = time.time()
mohex.sendCommand("dfpn-solve-state black")
elapsed = time.time() - start
print("Total Runtime: "+str(elapsed))


