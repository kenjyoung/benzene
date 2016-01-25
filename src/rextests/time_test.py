import time
from program import Program

PLAYERS = {0: "white", 1: "black"}
PASSCOLOR = '\033[92m'
FAILCOLOR = '\033[91m'
ENDCOLOR = '\033[0m'

def testallwins(program, size, opening, expected_wins):
	program.sendCommand("clear_board")
	program.sendCommand("boardsize "+str(size))
	toplay = 0
	for move in opening:
		program.sendCommand("play "+PLAYERS[toplay]+" "+move)
		toplay = not toplay
	wins = program.sendCommand("dfpn-solver-find-winning "+PLAYERS[toplay]).split()
	if set(expected_wins) == set(wins):
		return (True, [], [])
	else:
		false_losses = [x for x in expected_wins if x not in wins]
		false_wins = [x for x in wins if x not in expected_wins]
		return(False, false_losses, false_wins)

mohex = Program("../mohex/./mohex 2>/dev/null", True)
mohex.sendCommand("param_dfpn threads 4")
with open("rex5x5opening.dat") as f:
	lines = f.readlines()
	cases = [[line.split(':')[0],line.split(':')[1].split()] for line in lines]

fail_count = 0
start = time.time()
for case in cases:
	name = "5x5 "+case[0]
	opening = case[0]
	expected = case[1]
	test_start = time.time()
	result, false_losses, false_wins = testallwins(mohex,5,[opening],expected)
	if result:
		print(PASSCOLOR + name + " test passed! :)" + ENDCOLOR)
		elapsed = time.time() - test_start
		print("Runtime: "+str(elapsed))
	else:
		fail_count+=1
		print(FAILCOLOR + name + " test failed... :(" + ENDCOLOR)
		print("Misclassified as loss: "+str(false_losses))
		print("Misclassified as win: "+str(false_wins)+'\n')
elapsed = time.time() - start
if(fail_count):
	print(FAILCOLOR+str(fail_count)+" tests failed... :(" + ENDCOLOR)
else:
	print(PASSCOLOR+"All tests passed :)"+ENDCOLOR)
print("Total Runtime: "+str(elapsed))

