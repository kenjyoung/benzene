import time
from program import Program

PLAYERS = {0: "white", 1: "black"}
PASSCOLOR = '\033[92m'
FAILCOLOR = '\033[91m'
ENDCOLOR = '\033[0m'

def testwins(program, size, opening, expected_wins, expected_loses):
	program.sendCommand("clear_board")
	program.sendCommand("boardsize "+str(size))
	toplay = 0
	for move in opening:
		program.sendCommand("play "+PLAYERS[toplay]+" "+move)
		toplay = not toplay
	wins = program.sendCommand("dfpn-solver-find-winning "+PLAYERS[toplay]).split()
	if all(x in wins for x in expected_wins)\
	and not any(x in wins for x in expected_loses):
		return (True, [], [])
	else:
		false_losses = [x for x in expected_wins if x not in wins]
		false_wins = [x for x in expected_loses if x in wins]
		return(False, false_losses, false_wins)

mohex = Program("../mohex/./mohex", True)
mohex.sendCommand("param_dfpn threads 4")

name = "5x5 a1"
start = time.time()
result, false_losses, false_wins = testwins(mohex, 5, ["a1"], ["b1","c1","d1","e1","a2","b2","c2","e2","a3","b3","d3","e3","a4","c4","e4","a5","b5","c5","d5","e5"], ["d2","c3","b4","d4"])
elapsed = time.time() - start
if result:
	print(PASSCOLOR + name + " test passed! :)" + ENDCOLOR)
	print("Runtime: "+str(elapsed))
else:
	print(FAILCOLOR + name + " test failed... :(" + ENDCOLOR)
	print("Misclassified as loss: "+str(false_losses))
	print("Misclassified as win: "+str(false_wins)+'\n')