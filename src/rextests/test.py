from program import Program

PLAYERS = {0: "white", 1: "black"}
PASSCOLOR = '\033[92m'
FAILCOLOR = '\033[91m'
ENDCOLOR = '\033[0m'

def testoutcome(program, size, opening, expected_winner):
	program.sendCommand("clear_board")
	program.sendCommand("boardsize "+str(size))
	toplay = 0
	for move in opening:
		program.sendCommand("play "+PLAYERS[toplay]+" "+move)
		toplay = not toplay
	winner = program.sendCommand("dfpn-solve-state "+PLAYERS[toplay]).strip()
	return winner == expected_winner


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


#outcome test format: (<name>, [<boardsize>, <opening moves>, <expected winner>]")
outcome_tests = []
outcome_tests.append(("1x1", [1, [], "black"]))
outcome_tests.append(("3x3", [3, [], "black"]))
# outcome_tests.append(("5x5", [5, [], "black"]))
# outcome_tests.append(("5x5 a1 e5",[5, ["a1","e5"], "black"]))
# outcome_tests.append(("5x5 b1 a2",[5, ["b1","a2"], "black"]))
# outcome_tests.append(("5x5 b1 e4",[5, ["b1","e4"], "black"]))
# outcome_tests.append(("5x5 c3 a1",[5, ["c3","a1"], "black"]))
# outcome_tests.append(("5x5 c3 b1",[5, ["c3","b1"], "black"]))
# outcome_tests.append(("5x5 c3 d5",[5, ["c3","d5"], "black"]))
# outcome_tests.append(("5x5 c3 e5",[5, ["c3","e5"], "black"]))


#wins test format: (<name>, [<boardsize>, <opening moves>, <expected winning moves>, <expected losing moves>])
wins_tests = []
wins_tests.append(("2x2",[2, [], ["a1","b2"], ["b1","a2"]]))
wins_tests.append(("4x4", [4, [], ["a1","a2","a3","a4","b1","b2","b4","c1","c3","c4","d1","d2","d3","d4"], ["b3","c2"]]))
wins_tests.append(("3x3 a1", [3, ["a1"], ["a2","a3","b1","b3","c1","c2","c3"], ["b2"]]))
wins_tests.append(("3x3 b1", [3, ["b1"], ["a1","a2","a3","b2","b3","c1","c2","c3"], []]))
wins_tests.append(("3x3 c1", [3, ["c1"], ["a1","a2","a3","b1","b2","b3","c2","c3"], []]))
wins_tests.append(("3x3 a2", [3, ["a2"], ["a1","a3","b1","b3","c1","c2","c3"], ["b2"]]))
wins_tests.append(("3x3 b2", [3, ["b2"], ["a1","a2","a3","b1","b3","c1","c2","c3"], []]))

fail_count = 0
for name, test in outcome_tests:
	if testoutcome(mohex, test[0], test[1], test[2]):
		print(PASSCOLOR + name + " test passed! :)" + ENDCOLOR)
	else:
		fail_count+=1
		print(FAILCOLOR + name + " test failed... :(" + ENDCOLOR)
		print("Expected winner "+test[2]+" did not match computed.\n")

for name, test in wins_tests:
	result, false_losses, false_wins = testwins(mohex, test[0], test[1], test[2], test[3])
	if result:
		print(PASSCOLOR + name + " test passed! :)" + ENDCOLOR)
	else:
		fail_count+=1
		print(FAILCOLOR + name + " test failed... :(" + ENDCOLOR)
		print("Misclassified as loss: "+str(false_losses))
		print("Misclassified as win: "+str(false_wins)+'\n')

if fail_count == 0:
	print(PASSCOLOR+"All tests passed! :)"+ENDCOLOR)
else:
	print(FAILCOLOR+str(fail_count)+" tests failed. :("+ENDCOLOR)
