/* fast int primitives. min,max,abs,samesign
 *
 * WARNING: Assumes 2's complement arithmetic.
 *
 */


static __inline__ int intmax( register int x, register int y )
{
	return x < y ? y : x;
}

static __inline__ int intmin( register int x, register int y )
{
	return x < y ? x : y;
}

static __inline__ int intabs( register int x )
{
	return x < 0 ? -x : x;
}

#define fabsshift ((8*sizeof(unsigned int))-1)

#define signmask(x) (((int)x)>>fabsshift)
static __inline__ int intsamesign(int x, int y)
{
	return (y+(signmask(x) & -(y<<1)));
}
#undef signmask
#undef fabsshift

