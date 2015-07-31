import argparse
import random
from program import Program

PLAYERS = {0: "white", 1: "black"}

def get_wins(program, size, opening):
	program.sendCommand("clear_board")
	program.sendCommand("boardsize "+str(size))
	toplay = 0
	for move in opening:
		program.sendCommand("play "+PLAYERS[toplay]+" "+move)
		toplay = not toplay
	return program.sendCommand("dfpn-solver-find-winning "+PLAYERS[toplay])


p = argparse.ArgumentParser()
p.add_argument('moves', metavar = 'moves', type = int, help = 'number of moves in generated positions.')
p.add_argument('positions', metavar = 'positions', type = int, help = 'number of positions to generate.')
args = p.parse_args()

cells =  [chr(ord('a')+x)+str(y+1) for x in range(5) for y in range(5)]
openings = []

mohex = Program("../mohex/./mohex 2>/dev/null", False)
mohex.sendCommand("param_dfpn threads 4")

for p in range(args.positions):
	random.shuffle(cells)
	opening = cells[0:args.moves]
	for c in opening[0:-1]:
		print(c, end = " ")
	print(opening[-1], end = "")
	print(":", end = "")
	print(get_wins(mohex, 5, opening).strip())
