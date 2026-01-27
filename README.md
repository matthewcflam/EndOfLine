### Tron Light Cycle Game

Modify vga.c into a C program that plays the Tron light cycle on your FPGA
board.  Requirements for the game are described below.

The game is played with 2 players. One player must be a human, the other must
be a robot (described below).  You must determine which controls to use for the
human. (Optionally, you may allow a second human to force the robot to make a
turn.)

The game starts with an all-black screen. A white border should be drawn around
the perimeter of the screen, forming the gameplay area. The game designer can
add fixed obstacles (white). Obstacles may be single pixels, rectangles, lines,
or any other shape that you wish.  Collision with the border or an obstacle
results in death.

You can determine collision by reading the pixel colour from the VGA buffer at
the desired location, eg `pixel_t p = *pVGA;` will read the pixel colour at
location [0,0] into the variable p.

Each player controls a light cycle which starts at a fixed position roughly
halfway down the screen and one-third of the way from the left and right edges.
The light cycle is a single pixel that constantly moves across the screen and
leaves behind a coloured light trace. The player controls whether the light
cycle is moving up, down, left, or right, but it must always move by one pixel
after every time delay. The light trace is an electrified wall that kills any
player upon collision (including yourself!).

The winner is the first player to reach a score of 9. For each player, you must
always display the current score (0-9) using one 7-segment display each. A player
scores one point by surviving longer than the other player; it is possible that both
players die at the same time, in which case neither player scores a point. After a
complete game (reaching 9), the screen must fill up with the light of the winning
player.

For a simple robot player, have it look one and two pixels ahead. If it detects
anything other than an unclaimed pixel (still black), it first attempts to turn
LEFT. If the pixel to the LEFT is not black, then it attempts to turn RIGHT. If
it cannot turn LEFT or RIGHT, then it continues straight. You may modify this
behavior to make your robot player better.

This game is deliberately open-ended to allow you to either simplify or be
creative as you see fit.

It is recommended that you start with one working player (either human or robot),
then add the second player, then add the score-keeping and display.

