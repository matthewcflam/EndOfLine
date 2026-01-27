#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>  // for rand() and srand()
#include <time.h>    // for time() to seed the RNG

#include "address_map_niosv.h"

// from slides
#define MTIME_LO    (*(volatile uint32_t *)(MTIMER_BASE + 0x0))
#define MTIME_HI    (*(volatile uint32_t *)(MTIMER_BASE + 0x4))
#define MTIMECMP_LO (*(volatile uint32_t *)(MTIMER_BASE + 0x8))
#define MTIMECMP_HI (*(volatile uint32_t *)(MTIMER_BASE + 0xC))

#define KEY_INTMASK (*(volatile uint32_t *)(KEY_BASE + 0x8))
#define KEY_EDGE    (*(volatile uint32_t *)(KEY_BASE + 0xC))

typedef uint16_t pixel_t; 
/*
typedef renames an existing type - 1 6 bit unsigned int -> pixel_t
each pixel_t represents the colour
*/

volatile pixel_t *pVGA = (pixel_t *)FPGA_PIXEL_BUF_BASE;
volatile int *KEY = (int *)KEY_BASE; // updatePlayer
volatile int *SW = (int *)SW_BASE; // updateSpeed
volatile int *LEDR = (int *)LEDR_BASE; // updateSpeed
volatile uint32_t * const pHEX3_HEX0 = (uint32_t *) HEX3_HEX0_BASE; // updateScore
volatile uint32_t * const pHEX5_HEX4 = (uint32_t *) HEX5_HEX4_BASE; // updateScore

// CONSTANTS

const pixel_t blk = 0x0000; // 4 hex values = 65536, and 16 bits = 655356
const pixel_t wht = 0xffff;
const pixel_t red = 0xf800;
const pixel_t grn = 0x07e0;
const pixel_t blu = 0x001f;

const int half_y = MAX_Y/2;
const int half_x = MAX_X/2;
const int oneThird_x = MAX_X/3;
const int twoThird_x = oneThird_x * 2;

int x = oneThird_x;
int y = half_y;

// initial player direction = RIGHT
int dx = 1;
int dy = 0;

int rx = twoThird_x;
int ry = half_y;

//initial robot direction = LEFT
int rdy = 0;
int rdx = -1;

//initial scores
int score1 = 0;
int score2 = 0;

// these must be global so main can use them
int bothCrash = 0;
int playerCrash = 0;
int robotCrash = 0;

// === ENABLING INTERRUPTS === //

/* Disable global machine interrupts: read, clear MIE, write back */
static uint32_t disable_interrupts(void) {
    uint32_t mstatus;
    __asm__ volatile("csrr %0, mstatus" : "=r"(mstatus)); // read mstatus
    uint32_t old_mie = mstatus & (1u << 3);               // check MIE bit
    mstatus &= ~(1u << 3);                                // clear MIE bit
    __asm__ volatile("csrw mstatus, %0" :: "r"(mstatus)); // write back
    return old_mie;
}

/* Enable global machine interrupts: set mstatus.MIE = 1 */
static void enable_interrupts(void) {
    uint32_t mstatus;
    __asm__ volatile("csrr %0, mstatus" : "=r"(mstatus)); // read mstatus
    mstatus |= (1u << 3);                                 // set MIE bit
    __asm__ volatile("csrw mstatus, %0" :: "r"(mstatus)); // write back
}

/* Set allowed interrupt sources: write MIE register */
void setup_cpu_irqs(uint32_t mask) {
    __asm__ volatile("csrw mie, %0" :: "r"(mask));
}

/* Reads MTIME 64-bit value atomically */
static uint64_t mtime_read(void) {
    uint32_t hi1, lo, hi2;
    do {
        hi1 = MTIME_HI;
        lo  = MTIME_LO;
        hi2 = MTIME_HI;
    } while (hi1 != hi2);
    return ((uint64_t)hi2 << 32) | lo;
}

/* Writes MTIMECMP 64-bit value atomically (HIGH then LOW) */
static void mtimecmp_write(uint64_t t) {
    uint32_t old_mie = disable_interrupts();

    MTIMECMP_HI = (uint32_t)(t >> 32);
    MTIMECMP_LO = (uint32_t)(t & 0xFFFFFFFFu);

    if (old_mie) {
        enable_interrupts();
    }
}


// ==== SIMPLE METHODS BEGIN ==== //

void delay(int N)
{
    uint64_t start = mtime_read();        // snapshot time
    uint64_t target = start + N;    // target future time

    while (mtime_read() < target) {
        // wait -> while (condition) it actually just does nothing
    }
}

void drawPixel( int y, int x, pixel_t colour )
{
	*(pVGA + (y<<YSHIFT) + x ) = colour;
}

// return pixel_t (colour of pixel for obstacle detection)
pixel_t readPixel (int y, int x){
    pixel_t p = *(pVGA + (y<<YSHIFT) + x );
	return p;
}

/*
converts 8-bit rgb values into a 16-bit integer (RGB 565 - 5 red, 6 green, 5 blue)
*/
pixel_t makePixel( uint8_t r8, uint8_t g8, uint8_t b8 )
{
	// inputs: 8b of each: red, green, blue
	const uint16_t r5 = (r8 & 0xf8)>>3; // keep 5b red
	const uint16_t g6 = (g8 & 0xfc)>>2; // keep 6b green
	const uint16_t b5 = (b8 & 0xf8)>>3; // keep 5b blue
	return (pixel_t)( (r5<<11) | (g6<<5) | b5 );
}

/*
draws a filled rectangle with the colour c
*/
void rect( int y1, int y2, int x1, int x2, pixel_t c )
{
	for( int y=y1; y<y2; y++ )
		for( int x=x1; x<x2; x++ )
			drawPixel( y, x, c );
}

void perimeter(int y1, int y2, int x1, int x2, pixel_t c){

    for(int y=y1; y<y2; y++){
		for(int x=x1; x<x2; x++){
            if (y == y1 || y == y2 - 1){
                drawPixel(y, x, c);
            }
			if (x == x1 || x == x2 -1){
                drawPixel(y, x, c);
            }
        }
    }
}

int randInRange(int min, int max) {
    return rand() % (max - min) + min;  // returns min..max-1
}

/*
// add starting obstacles in white
void obstacle(int y1, int y2, int x1, int x2, pixel_t c){
	srand(time(NULL));

    for(int count = 0; count < 10; count++){
		int randY = 0;
		int randX = 0;

		while(1){
        	randY = randInRange(y1, y2);
        	randX = randInRange(x1, x2);

			int nearSpawn1 = abs(randY - y) < 10 && abs(randX - x) < 10;

            int nearSpawn2 = abs(randY - ry) < 10 && abs(randX - rx) < 10;

            if (!nearSpawn1 && !nearSpawn2)
                break;  // good location
		}

		rect(randY - 5, randY + 5, randX - 5, randY + 5, c); // generate a 10x10 rectangle
    }
}
*/

void obstacle(int y1, int y2, int x1, int x2, pixel_t c){
    for(int count = 0; count < 5; count++){
        int randY = randInRange(y1, y2);
        int randX = randInRange(x1, x2);

		rect(randY - 2, randY + 2, randX - 2, randY + 2, c); // generate a rectangle
    }
}

int isCollision(int y, int x) {
    pixel_t p = readPixel(y, x);
    if(p != blk){
        return 1;
    }
    return 0;  // 1 if collision, 0 if empty
}

// void updatePlayer() {
// 	static int prev_keys = 0;

//     int keys = *KEY;
//     int changed = (keys ^ prev_keys) & keys;   // only react to new presses

//     // left turn
//     if (changed & 0x1) {
//         int old_dx = dx;
//         dx = dy;       
//         dy = -old_dx;   
//     }

//     // right turn
//     if (changed & 0x2) {
//         int old_dx = dx;
//         dx = -dy;      
//         dy = old_dx;   
//     }

//     prev_keys = keys;
// }

void updateRobot(int ry, int rx){

	if(isCollision(ry + rdy, rx + rdx) || isCollision(ry + 2*rdy, rx + 2*rdx)){//if you can't go straight

		//turn left
		int leftdx = rdy;
		int leftdy = -rdx;
		if(isCollision(ry + leftdy, rx + leftdx) || isCollision(ry + 2*leftdy, rx + 2*leftdx)){//if you can't go left

			//turn right
			int rightdx = -rdy;
			int rightdy = rdx;
			if(isCollision(ry + rightdy, rx + rightdx) || isCollision(ry + 2*rightdy, rx + 2*rightdx)){ //if you can't go right
				//go straight
				rdy = rdy;
				rdx = rdx;
			}
			
			//if you can go right
			rdx = rightdx;
			rdy = rightdy;
		}

		//if you can go left
		rdx = leftdx;
		rdy = leftdy;
	}
	rdy = rdy;
	rdx = rdx;
}

int updateSpeed (){
	*LEDR = 0x0; //reset LEDS

	int switches = *SW;
	int speed = 3000000;	

	if(switches & 0x4){ // third switch
		speed = 30000000;
		*LEDR = 0x4;
	}

	if(switches & 0x8){ // fourth switch
		speed = 300000000;
		*LEDR = 0x8;
	}

	return speed;
}

const uint8_t HEX_DIGITS[10] = {
    0x3F, // 0
    0x06, // 1
    0x5B, // 2
    0x4F, // 3
    0x66, // 4
    0x6D, // 5
    0x7D, // 6
    0x07, // 7
    0x7F, // 8
    0x6F  // 9
};
const uint8_t HEX_BLANK = 0x00;

void displayScore(int score1, int score2) {
    uint8_t data1 = HEX_DIGITS[score1 % 10]; 
    uint8_t data2 = HEX_DIGITS[score2 % 10]; 

	uint32_t hex0_3 = 0;
    hex0_3 |= ((uint32_t)data1);               // HEX0 (bits 7:0)
    hex0_3 |= ((uint32_t)data2) << 8;          // HEX1 (bits 15:8)
    hex0_3 |= ((uint32_t)HEX_BLANK) << 16;  // HEX2 (bits 23:16)
    hex0_3 |= ((uint32_t)HEX_BLANK) << 24;  // HEX3 (bits 31:24)

    *pHEX3_HEX0 = hex0_3;

    uint32_t hex4_5 = ((uint32_t)HEX_BLANK)       // HEX4 (bits 7:0)
                     | ((uint32_t)HEX_BLANK << 8); // HEX5 (bits 15:8)
    *pHEX5_HEX4 = hex4_5;
}

void reset(){
	rect( 0, MAX_Y, 0, MAX_X, blk );
	perimeter ( 0, MAX_Y, 0, MAX_X, wht );
	obstacle (0, MAX_Y - 5, 0, MAX_X - 5, wht);

	bothCrash = 0;
	playerCrash = 0;
	robotCrash = 0;

	//reset all values (typically defined at the start of the class)
	x = oneThird_x;
	y = half_y;
	dx = 1;
	dy = 0;
	rx = twoThird_x;
	ry = half_y;
	rdy = 0;
	rdx = -1;

	drawPixel(y, x, blu);
	drawPixel(ry, rx, red);
}



// === ISRs and handler === //

void timer_isr(void);
void key_isr(void);

/* Machine-level trap handler */
void handler(void) __attribute__((interrupt("machine")));
void handler(void) {
    uint32_t cause;
    __asm__ volatile("csrr %0, mcause" : "=r"(cause));

    uint32_t is_interrupt = cause >> 31;        // MSB
    uint32_t code         = cause & 0x7FFFFFFF; // bits [30:0]

    if (!is_interrupt) {
        // ignore exceptions for this lab
        return;
    }

    // CPEN labs: timer is usually IRQ 7, KEY as some external IRQ.
    if (code == 7) {          // Machine timer IRQ
        timer_isr();
        return;
    }
    if (code == 18) {         // External IRQ for KEY (per system)
        key_isr();
        return;
    }
}

/* Install handler into mtvec and enable IRQs 7 (timer) and 18 (KEY) */
static void init_interrupts(void) {
    uintptr_t handler_addr = (uintptr_t)&handler;
    __asm__ volatile("csrw mtvec, %0" :: "r"(handler_addr));

    uint32_t irq_mask = (1u << 7) | (1u << 18);
    setup_cpu_irqs(irq_mask);

    enable_interrupts();
}

volatile int pending_turn = 0;

// FOR MTIME
void timer_isr(void) {

    // Set next interrupt time
	int speed = updateSpeed();
	uint64_t future = mtime_read() + speed;
	mtimecmp_write(future);

	// === THIS REPLACES UPDATEPLAYER ====
		// execute pending left turn
		if(pending_turn == 1){
			int old_dx = dx;
			dx = dy;       
			dy = -old_dx;
			pending_turn = 0;
		}

		// execute pending right turn
		if (pending_turn == 2) {
			int old_dx = dx;
			dx = -dy;      
			dy = old_dx;  
			pending_turn = 0;	 
		}

	int playerNextCollision = isCollision(y + dy, x + dx);
	int robotNextCollision  = isCollision(ry + rdy, rx + rdx);

	updateRobot(ry, rx); // goes after for the "run into each other scenario"

	if(playerNextCollision && robotNextCollision){
	//if both crash
		bothCrash = 1;
	}
	else if(robotNextCollision && !playerNextCollision){
	//if robot crashes
		robotCrash = 1;
	}
	else if(playerNextCollision && !robotNextCollision){
		//if player crashes
		playerCrash = 1;
	}
	else{
		//if both are safe, move both.
		y = y+dy;
		x = x+dx;
		drawPixel(y, x, blu);

		ry = ry + rdy;
		rx = rx + rdx;
		drawPixel(ry, rx, red);
	}

}

void key_isr(void) {
	uint32_t edges = KEY_EDGE;
    KEY_EDGE = edges; // clear edge flags

    int key0 = edges & 0x1;
    int key1 = edges & 0x2;

	if (key0) {
        pending_turn = (pending_turn == 1) ? 0 : 1;
		*LEDR = 0x2;
    } else if (key1) {
        pending_turn = (pending_turn == 2) ? 0 : 2;
		*LEDR = 0x1;
    }
}


int main(){
	srand(time(NULL));
	displayScore(0,0);
	reset();

	KEY_INTMASK = 0x3;
	KEY_EDGE= 0x3;

	// Initialize mtimecmp interrupt
    int speed = updateSpeed(); //speed depends on SWITCHES
	uint64_t future = mtime_read() + speed;
	mtimecmp_write(future);

	init_interrupts();

	printf( "start\n" );
	
	while(score1 < 9 && score2 < 9){
		if(bothCrash){
			//if both crash
			reset();
		}
		else if(robotCrash){
			//if robot crashes
			score1++;
			reset();
			displayScore(score2, score1);
		}
		else if(playerCrash){
			//if player crashes
			score2++;
			reset();
			displayScore(score2, score1);
		}
	}
		
	//win screen
	if(score1 == 9){
		rect(0, MAX_Y, 0, MAX_X, blu);
	}
	if (score2 == 9) {
		rect(0, MAX_Y, 0, MAX_X, red);
	}

	printf( "done\n" );
}