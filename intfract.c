// ORIGINAL CODE TAKEN FROM https://github.com/rahra/intfract.git
// VASTLY REDUCED FOR CPEN211, 2025, Guy Lemieux

/* Copyright 2015-2025 Bernhard R. Fischer, 4096R/8E24F29D <bf@abenteuerland.at>
 *
 * IntFract is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3 of the License.
 *
 * IntFract is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with IntFract. If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdint.h>

typedef int64_t nint_t;

#define NORM_FACT ((nint_t)1 << NORM_BITS)
#define NORM_BITS 29

// maximum number of iterations of the inner loop
#define MAXITERATE 64
#define IT8(x) ((x) * 255 / maxiterate_)


int maxiterate_ = MAXITERATE;

typedef uint16_t pixel_t;
volatile pixel_t *pVGA = (volatile pixel_t *)0x08000000;


#define MAX_X  320
#define MAX_Y 240
#define YSHIFT 9


/*! This function contains the iteration loop using integer arithmetic.
 * @param real0 Real coordinate of pixel within the complex plane.
 * @param imag0 Imaginary coordinate of the pixel.
 * @return Returns the number of iterations to reach the break condition.
 */
int iterate(nint_t real0, nint_t imag0)
{
   nint_t realq, imagq, real, imag;
   int i;

   real = real0;
   imag = imag0;
   for (i = 0; i < maxiterate_; i++)
   {
     realq = (real * real) >> NORM_BITS;
     imagq = (imag * imag) >> NORM_BITS;
     if ((realq + imagq) > (nint_t) 4 * NORM_FACT)
        break;
     imag = ((real * imag) >> (NORM_BITS - 1)) + imag0;
     real = realq - imagq + real0;
   }
   return i;
}


// Translate iteration count (0..64) into an RGB color value
pixel_t fract_color(unsigned int itcnt)
{
	return (pixel_t)(itcnt >= maxiterate_ ?
					 0 : (IT8(itcnt)/2<<11/*red*/  )
					   | (IT8(itcnt)<<5   /*green*/)
					   | (IT8(itcnt)/2    /*blue*/ ) );
}

void setPixel( int y, int x, int itcnt )
{
	*(pVGA + (y<<YSHIFT) + x) = fract_color( itcnt );
}


/*! This function contains the outer loop, i.e. calculate the coordinates
 * within the complex plane for each pixel and then call iterate().
 * @param image Pointer to image array of size hres * vres elements.
 * @param realmin Minimun real value of image.
 * @param imagmin Minimum imaginary value of image.
 * @param realmax Maximum real value.
 * @param imagmax Maximum imaginary value.
 * @param hres Pixel width of image.
 * @param vres Pixel height of image.
 * @param start Column to start calculation with.
 * @param skip Number of columns to skip before starting with the next column.
 */

void mand_calc(
	nint_t realmin, nint_t imagmin, nint_t realmax, nint_t imagmax,
	int hres, int vres, int start, int skip )
{
  nint_t deltareal, deltaimag, real0,  imag0;
  int x, y;

  deltareal = realmax - realmin;
  deltaimag = imagmax - imagmin;

  int col;
  for (x = start; x < hres; x += skip)
  {
    real0 = realmin + deltareal * x / hres;
    for (y = 0; y < vres;)
    {
      imag0 = imagmax - deltaimag * y / vres;

      col = iterate( real0, imag0 ); // this is the compute-intensive part

      // fill all pixels which are below int resolution with the same iteration value
      for (int _y = 0; y < vres && deltaimag * _y < vres; _y++, y++) {
         // *(image + x + hres * (vres - y - 1)) = col;
         //setPixel( vres-y-1, x, col );
         setPixel( y, x, col );
      }
    }
  }

}

void blankscreen()
{
	for( int y=0; y<MAX_Y; y++ )
		for( int x=0; x<MAX_X; x++ )
			setPixel( y, x, 0 );

}

/* saved locations in the mandelbrot image */
float bbox[] = {
	-2.00, -1.20, 0.70, 1.20,
	-1.82, -0.07, -1.7, 0.07,
	-1.769, -0.05715, -1.7695, -0.0567,
	-1.3, 0.03, -1.24, 0.1

};
int num_bb = sizeof(bbox) / sizeof(float);

void infinite_loop()
{
	int width = MAX_X/2, height = MAX_Y/2;       // 1/4 of the screen

	while( 1 ) {
		for( int bb=0; bb < num_bb; bb+=4 ) {
			mand_calc(
				bbox[bb+0] * NORM_FACT, bbox[bb+1] * NORM_FACT, bbox[bb+2] * NORM_FACT, bbox[bb+3] * NORM_FACT,
				width, height, 0, 1 );
		}
	}

}

int main()
{
	blankscreen();
	infinite_loop();
	return 0;
}