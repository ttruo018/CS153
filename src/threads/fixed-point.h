#ifndef FIXED_POINT_H
#define FIXED_POINT_H

typedef int real;

static real fixed_point_number(real num, real denom);
static real fixed_point_mult(real a, real b);
static real fixed_point_divide(real a, real b);
static real fixed_point_round_zero(real x);
static real fixed_point_round_near(real x);

static real fixed_point_number(real num, real denom)
{
	return (num * (1 << 14)) / denom;
}

static real fixed_point_mult(real a, real b)
{
	return ((int64_t) a) * b / (1 << 14);
}

static real fixed_point_divide(real a, real b)
{
	return ((int64_t) a) * (1 << 14) / b;
}

static real fixed_point_round_zero(real x)
{
	return x / (1 << 14);	
}

static real fixed_point_round_near(real x)
{
	return (x >= 0) ? ((x + (1 << 14) / 2) / (1 << 14)) : ((x - (1 << 14) / 2) / (1 << 14));
}

#endif
