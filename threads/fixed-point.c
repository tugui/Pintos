#include "threads/fixed-point.h"

void
float_init (struct float_number *f, int integer, int demical, int demical_digits)
{
	f->demical_digits = demical_digits;
	f->value = integer * (1 << demical_digits) + demical;	
}
   
struct
float_number add_int (struct float_number f, int i)
{
	f.value += i * (1 << f.demical_digits);
	return f;		
}

struct float_number
add_float (struct float_number f1, struct float_number f2)
{
	struct float_number f;
	f.demical_digits = f1.demical_digits;
	f.value = f1.value + f2.value;
	return f;
}
		   
struct float_number
subtract_int (struct float_number f, int i)
{
	f.value -= i * (1 << f.demical_digits);
	return f;
}

struct float_number
subtract_float (struct float_number f1, struct float_number f2)
{
	struct float_number f;
	f.demical_digits = f1.demical_digits;
	f.value = f1.value - f2.value;
	return f;
}

struct float_number
multiply_int (struct float_number f, int i)
{
	f.value *= i;
	return f;
}

struct float_number
multiply_float (struct float_number f1, struct float_number f2)
{
	struct float_number f;
	f.demical_digits = f1.demical_digits;
	f.value = (int64_t) f1.value * f2.value / (1 << f1.demical_digits);
	return f;
}

struct float_number
divide_int (struct float_number f, int i)
{
	f.value /= i;
	return f;
}

struct float_number
divide_float (struct float_number f1, struct float_number f2)
{
	struct float_number f;
	f.demical_digits = f1.demical_digits;
	f.value = (int64_t) f1.value * (1 << f1.demical_digits) / f2.value;
	return f;
}

int
float_to_int_zero (struct float_number f)
{
	return f.value / (1 << f.demical_digits);
}

int
float_to_int_near (struct float_number f)
{
	if (f.value >= 0)
		return	(f.value + (1 << f.demical_digits) / 2) / (1 << f.demical_digits);
	else
		return	(f.value - (1 << f.demical_digits) / 2) / (1 << f.demical_digits);
}
