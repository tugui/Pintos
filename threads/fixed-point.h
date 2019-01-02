#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

#include <stdint.h>

struct float_number
	{
		int value;
		int demical_digits; /* Default value is 14. */
	};

void float_init (struct float_number *, int, int, int);

struct float_number add_int (struct float_number , int);
struct float_number add_float (struct float_number, struct float_number);

struct float_number subtract_int (struct float_number, int);
struct float_number subtract_float (struct float_number, struct float_number);

struct float_number multiply_int (struct float_number, int);
struct float_number multiply_float (struct float_number, struct float_number);

struct float_number divide_int (struct float_number, int);
struct float_number divide_float (struct float_number, struct float_number);

int float_to_int_zero (struct float_number);
int float_to_int_near (struct float_number);

#endif
