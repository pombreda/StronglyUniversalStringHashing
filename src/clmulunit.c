#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "clmul.h"
#include "clmulpoly64bits.h"
#include "clhash.h"
#include "clmulhierarchical64bits.h"

#ifdef __PCLMUL__

int is_zero(__m128i a) {
	return _mm_testz_si128(a,a);
}

// checks to see if a == b
int equal(__m128i a, __m128i b) {
	__m128i xorm = _mm_xor_si128(a,b);
	return _mm_testz_si128(xorm,xorm);

}

void printme32(__m128i v1) {
	printf(" %u %u %u %u  ", _mm_extract_epi32(v1,0), _mm_extract_epi32(v1,1), _mm_extract_epi32(v1,2), _mm_extract_epi32(v1,3));
}

void printme64(__m128i v1) {
	printf(" %llu %llu  ", _mm_extract_epi64(v1,0), _mm_extract_epi64(v1,1));
}

// useful for debugging, essentially determines the most significant bit set
// return 0 if a is made solely of zeroes
int degree(__m128i a) {
	int x4 = _mm_extract_epi32 (a,3);
	if(x4 != 0) {
		return 32 - __builtin_clz(x4) + 3 * 32 - 1;
	}
	int x3 = _mm_extract_epi32 (a,2);
	if(x3 != 0) {
		return 32 - __builtin_clz(x3) + 2 * 32 - 1;
	}
	int x2 = _mm_extract_epi32 (a,1);
	if(x2 != 0) {
		return 32 - __builtin_clz(x2) + 32 - 1;
	}
	int x1 = _mm_extract_epi32 (a,0);
	if(x1 != 0) {
		return 32 - __builtin_clz(x1) - 1;
	}
	return 0;
}

typedef struct {
	__m128i x;
	__m128i y;
}PairOfVec;

// 128bit x 128bit to 128bit multiplication
__m128i fullcarrylessmultiply(__m128i a, __m128i b) {
	__m128i part1 = _mm_clmulepi64_si128( a, b, 0x00);
	__m128i part2 = _mm_clmulepi64_si128( a, b, 0x10);
	part2 = _mm_slli_si128(part2,8);
	__m128i part3 = _mm_clmulepi64_si128( a, b, 0x01);
	part3 = _mm_slli_si128(part3,8);
	return _mm_xor_si128( _mm_xor_si128(part1,part2),part3);

}

__m128i unibit(int off) {
	__m128i bitf = off>=64 ? _mm_set_epi64x(1ULL<<(off-64),0) : _mm_set_epi64x(0,1ULL<<off);
	return bitf;
}

// useful for debugging. Divides a by b in carryless model
// surely, there is a better way to do this..., the x part of PairOfVec is the
// result of the division and the y part is the remainder
PairOfVec slowcarrylessdivision(__m128i a, __m128i b) {
	__m128i copya = a;
	int degreea = degree(a);
	int degreeb = degree(b);
	PairOfVec result;
	result.x = _mm_setzero_si128();
	while(degreea > 0) {
		int off = degreea - degreeb;
		if(off<0) break;
		__m128i bitf = unibit(off);
		int degreebitf = degree(bitf);
		if( degreebitf + degreeb != degreea) {
			printme64(bitf);
			printf("you would think that this would work %i %i %i %i \n", degreebitf,degreeb,degreea,off);
		}
		result.x = _mm_xor_si128(bitf,result.x);
		__m128i rmult = fullcarrylessmultiply(b, bitf);
		a = _mm_xor_si128(a,rmult);
		int newdegreea = degree(a);
		if(newdegreea >= degreea)
		printf("this will not end well\n");
		degreea = newdegreea;
	}
	result.y = a;
	// now we check our result
	if(!equal(_mm_xor_si128(fullcarrylessmultiply(result.x, b),result.y),copya)) {
		printf("Slow division is buggy\n");
		abort();
	};
	return result;
}

// WARNING: HIGH 64 BITS CONTAIN GARBAGE, must call _mm_cvtsi128_si64 to get
// meaningful bits.
__m128i barrettWithoutPrecomputation64_si128( __m128i A) {
	///http://www.jjj.de/mathdata/minweight-primpoly.txt
	// it is important, for the algo. we have chosen that 4 is smaller
	// equal than 32=64/2

	const int n = 64;// degree of the polynomial
	const __m128i C = _mm_set_epi64x(1U,(1U<<4)+(1U<<3)+(1U<<1)+(1U<<0));// C is the irreducible poly. (64,4,3,1,0)
	/////////////////
	/// This algo. requires two multiplications (_mm_clmulepi64_si128)
	/// They are probably the bottleneck.
	/// Note: Barrett's original algorithm also required two multiplications.
	////////////////
	assert(n/8==8);
	__m128i Q2 = _mm_clmulepi64_si128( A, C, 0x01);
	Q2 = _mm_xor_si128(Q2,A);
	const __m128i Q4 = _mm_clmulepi64_si128( Q2, C, 0x01);
	const __m128i final = _mm_xor_si128 (A, Q4);
	return final;/// WARNING: HIGH 64 BITS CONTAIN GARBAGE
}

uint64_t barrettWithoutPrecomputation64( __m128i A) {
	const __m128i final = barrettWithoutPrecomputation64_si128(A);
	return _mm_cvtsi128_si64(final);
}


void displayfirst() {
	for(uint64_t a = 0; a< 16; ++a) {
		const __m128i A = _mm_set_epi64x(a,0);
		__m128i fastmod = barrettWithoutPrecomputation64_si128(A);
		printf(" %llu, ",_mm_extract_epi64(fastmod,0));

	}
	printf("\n");
}

void clmulunittest0_64() {
	printf("CLMUL test 0_64...\n");
	const __m128i C = _mm_set_epi64x(1U,(1U<<4)+(1U<<3)+(1U<<1)+(1U<<0)); // C is the irreducible poly. (64,4,3,1,0)
	uint64_t mul1 = 4343232+(1ULL<<63)+(1ULL<<60)+(1ULL<<45);//random-like
	uint64_t mul2 = 12344567788889+(1ULL<<62)+(1ULL<<61)+(1ULL<<55);//random-like
	for(uint64_t a = 1; a< 1024; ++a) {
		const __m128i A = _mm_set_epi64x(mul1*a,mul2*a);
		__m128i sillymod = slowcarrylessdivision(A,A).y;
		if(!is_zero(sillymod)) {
			printme64(sillymod);
			printf("silly mod is not zero?\n");
			abort();
		}
		PairOfVec AdivC = slowcarrylessdivision(A,C);
		__m128i slowmod = AdivC.y;
		__m128i fastmod = barrettWithoutPrecomputation64_si128(A);
		fastmod = _mm_and_si128(fastmod,_mm_set_epi64x(0,-1)); // keep just the low 64 bits

		if(!equal(slowmod,fastmod)) {
			printf("slowmod = ");
			printme64(slowmod);
			printf("\n");
			printf("fastmod = ");
			printme64(fastmod);
			printf("\n");
			printf("64-bit bug slowmod and fastmod differs\n");
			abort();
		}
	}
	printf("Test passed!\n");

}

void precompclmulunittest0_64() {
	printf("CLMUL test precomp 0_64...\n");
	const __m128i C = _mm_set_epi64x(1U,(1U<<4)+(1U<<3)+(1U<<1)+(1U<<0)); // C is the irreducible poly. (64,4,3,1,0)
	uint64_t mul1 = 4343232+(1ULL<<63)+(1ULL<<60)+(1ULL<<45);//random-like
	uint64_t mul2 = 12344567788889+(1ULL<<62)+(1ULL<<61)+(1ULL<<55);//random-like
	for(uint64_t a = 1; a< 1024; ++a) {
		const __m128i A = _mm_set_epi64x(mul1*a,mul2*a);
		__m128i sillymod = slowcarrylessdivision(A,A).y;
		if(!is_zero(sillymod)) {
			printme64(sillymod);
			printf("silly mod is not zero?\n");
			abort();
		}
		PairOfVec AdivC = slowcarrylessdivision(A,C);
		__m128i slowmod = AdivC.y;
		__m128i fastmod = precompReduction64_si128(A);
		fastmod = _mm_and_si128(fastmod,_mm_set_epi64x(0,-1)); // keep just the low 64 bits

		if(!equal(slowmod,fastmod)) {
			printf("slowmod = ");
			printme64(slowmod);
			printf("\n");
			printf("fastmod = ");
			printme64(fastmod);
			printf("\n");
			printf("64-bit bug slowmod and fastmod differs\n");
			abort();
		}
	}
	printf("Test passed!\n");

}

void clmulunittest0_32() {
	printf("CLMUL test 0_32...\n");
	const uint64_t irredpoly = 1UL+(1UL<<2)+(1UL<<6)+(1UL<<7)+(1UL<<32);
	const __m128i C = _mm_set_epi64x(0,irredpoly); // C is the irreducible poly.
	uint64_t mul2 = 12344567788889+(1ULL<<62)+(1ULL<<61)+(1ULL<<55);//random-like
	for(uint64_t a = 1; a< 1024; ++a) {
		const __m128i A = _mm_set_epi64x(0,mul2*a);
		__m128i sillymod = slowcarrylessdivision(A,A).y;
		if(!is_zero(sillymod)) {
			printme64(sillymod);
			printf("silly mod is not zero?\n");
			abort();
		}
		__m128i slowmod = slowcarrylessdivision(A,C).y;
		__m128i fastmod = barrettWithoutPrecomputation32_si128(A);
		fastmod = _mm_and_si128(fastmod,_mm_set_epi32(0,0,0,-1)); // keep just the low 32 bits
		if(!equal(slowmod,fastmod)) {
			printf("slowmod = ");
			printme32(slowmod);
			printf("\n");
			printf("fastmod = ");
			printme32(fastmod);
			printf("\n");
			printf("32-bit bug slowmod and fastmod differs\n");
			abort();
		}
	}
	printf("Test passed!\n");
}


__m128i barrettWithoutPrecomputation16_si128( __m128i A) {
	///http://www.jjj.de/mathdata/minweight-primpoly.txt
	const uint64_t irredpoly = 1UL+(1UL<<2)+(1UL<<3)+(1UL<<5)+(1UL<<16);
	// it is important, for the algo. we have chosen that 5 is smaller
	// equal than 8=16/2
	//const int n = 16;// degree of the polynomial
	const __m128i C = _mm_set_epi64x(0,irredpoly);// C is the irreducible poly.
	const __m128i Q1 = _mm_srli_si128 (A, 2);
	const __m128i Q2 = _mm_clmulepi64_si128( Q1, C, 0x00);// A div x^n
	const __m128i Q3 = _mm_srli_si128 (Q2, 2);
	const __m128i Q4 = _mm_clmulepi64_si128( Q3, C, 0x00);
	const __m128i final = _mm_xor_si128 (A, Q4);
	return final;
}

uint16_t barrettWithoutPrecomputation16( __m128i A) {
	return (uint16_t) _mm_cvtsi128_si32(barrettWithoutPrecomputation16_si128(A));
}

void clmulunittest0_16() {
	printf("CLMUL test 0_16...\n");
	const uint64_t irredpoly = 1UL+(1UL<<2)+(1UL<<3)+(1UL<<5)+(1UL<<16);
	const __m128i C = _mm_set_epi64x(0,irredpoly); // C is the irreducible poly.
	uint32_t mul2 = 2979263833U+(1U<<31);//random-like
	for(uint64_t a = 1; a< 1024; ++a) {
		const __m128i A = _mm_set_epi32(0,0,0,mul2*a);
		__m128i sillymod = slowcarrylessdivision(A,A).y;
		if(!is_zero(sillymod)) {
			printme64(sillymod);
			printf("silly mod is not zero?\n");
			abort();
		}
		__m128i slowmod = slowcarrylessdivision(A,C).y;
		__m128i fastmod = barrettWithoutPrecomputation16_si128(A);
		fastmod = _mm_and_si128(fastmod,_mm_set_epi32(0,0,0,0xFFFF)); // keep just the low 16 bits
		if(!equal(slowmod,fastmod)) {
			printf("slowmod = ");
			printme32(slowmod);
			printf("\n");
			printf("fastmod = ");
			printme32(fastmod);
			printf("\n");
			printf("32-bit bug slowmod and fastmod differs\n");
			abort();
		}
	}
	printf("Test passed!\n");
}
void clmulunittest1() {
	printf("CLMUL test 1...\n");
	// A * x = y ought to be invertible.
	// brute force check is hard, but we can do some checking
	// in the 16-bit case
	for(int a = 1; a< 1<<16; a+=32) {
		__m128i A = _mm_set_epi32(0,0,0,2*a);
		int counter[1<<16];
		for(int j = 0; j < 1<<16; ++j) counter[j] = 0;
		for(int k = 0; k< 1<<16; ++k) {
			__m128i v1 = _mm_set_epi16(0,0,0,0,0,0,0,k);
			__m128i clprod1 = _mm_clmulepi64_si128( A, v1, 0x00);
			uint64_t m64 = barrettWithoutPrecomputation64(clprod1);
			uint32_t m32 = barrettWithoutPrecomputation32(_mm_set_epi64x(0,m64));
			uint16_t m16 = barrettWithoutPrecomputation16(_mm_set_epi32(0,0,0,m32));
			counter[m16]++;
		}
		for(int j = 0; j < 1<<16; ++j) {
			if(counter[j] != 1) {
				printf("j= %i c = %i\n",j,counter[j]);
				printf("bug\n");
				abort();
			}

		}
	}
}

void clmulunittest2() {
	printf("CLMUL test 2...\n");
	// Idea: we fish for two non-zero 32-bit integers with a zero product
	for(int a = 1; a< 1<<16; a+=32) {
		__m128i A = _mm_set_epi32(0,0,0,2*a);
		for(int k = 1; k< 1<<16; ++k) {
			__m128i v1 = _mm_set_epi16(0,0,0,0,0,0,k,0);
			__m128i clprod1 = _mm_clmulepi64_si128( A, v1, 0x00);
			uint64_t m64 = barrettWithoutPrecomputation64(clprod1);
			uint32_t m32 = barrettWithoutPrecomputation32(_mm_set_epi64x(0,m64));
			if(m32 == 0) {
				printf("bug\n");
				abort();
			}
		}
	}
}

void clmulunittest3() {
	printf("CLMUL test 3...\n");
	// Idea: we fish for two non-zero 64-bit integers with a zero product
	for(int a = 1; a< 1<<16; a+=32) {
		__m128i A = _mm_set_epi32(0,0,0,2*a);
		for(int k = 1; k< 1<<16; ++k) {
			__m128i v1 = _mm_set_epi16(0,0,0,0,k,0,0,0);
			__m128i clprod1 = _mm_clmulepi64_si128( A, v1, 0x00);
			uint64_t m64 = barrettWithoutPrecomputation64(clprod1);
			if(m64 == 0) {
				printf("A=");
				printme64(v1);
				printf("\n");
				printf("v1=");
				printme64(v1);
				printf("\n");
				printf("clprod1=");
				printme64(clprod1);
				printf("\n");
				printf("bug\n");
				abort();
			}
		}
	}
}
void clmulunittest3a() {
	printf("CLMUL test 3a...\n");
	// Idea: we fish for two non-zero 64-bit integers with a zero product
	for(int a = 1; a< 1<<16; a+=32) {
		__m128i A = _mm_set_epi32(0,0,2*a,0);
		for(int k = 1; k< 1<<16; ++k) {
			__m128i v1 = _mm_set_epi16(0,0,0,0,k,0,0,0);
			__m128i clprod1 = _mm_clmulepi64_si128( A, v1, 0x00);
			uint64_t m64 = barrettWithoutPrecomputation64(clprod1);
			if(m64 == 0) {
				printf("A=");
				printme64(v1);
				printf("\n");
				printf("v1=");
				printme64(v1);
				printf("\n");
				printf("clprod1=");
				printme64(clprod1);
				printf("\n");
				printf("bug\n");
				abort();
			}
		}
	}
}

// we compute key**2 value1 + key * value2 + value3
uint64_t basichorner(uint64_t key, uint64_t value1, uint64_t value2, uint64_t value3) {
	__m128i tkey1 = _mm_set_epi64x(0,key);
	__m128i acc = _mm_set_epi64x(0,value1);
	__m128i multi = _mm_clmulepi64_si128( acc, tkey1, 0x00);
	acc = precompReduction64_si128(multi);
	acc = _mm_xor_si128 (acc,_mm_set_epi64x(0,value2));
	multi = _mm_clmulepi64_si128( acc, tkey1, 0x00);
	acc = precompReduction64_si128(multi);
	acc = _mm_xor_si128 (acc,_mm_set_epi64x(0,value3));
	return _mm_cvtsi128_si64(acc);
}
// we compute key**2 value1 + key * value2 + value3
uint64_t fasthorner(uint64_t key, uint64_t value1, uint64_t value2, uint64_t value3) {
	__m128i tkey1 = _mm_set_epi64x(0,key);
	__m128i tkey2 = precompReduction64_si128(_mm_clmulepi64_si128( tkey1, tkey1, 0x00));
	__m128i prod1 = _mm_clmulepi64_si128( _mm_set_epi64x(0,value1), tkey2, 0x00);
	__m128i prod2 = _mm_clmulepi64_si128( _mm_set_epi64x(0,value2), tkey1, 0x00);
	__m128i x = _mm_set_epi64x(0,value3);
	__m128i acc = _mm_xor_si128 (x,prod2);
	acc = _mm_xor_si128 (acc,prod1);
	acc = precompReduction64_si128(acc);
	return _mm_cvtsi128_si64(acc);
}

// we compute key**2 value1 + key * value2 + value3
uint64_t fasthorner2(uint64_t k, uint64_t value1, uint64_t value2, uint64_t value3) {
	__m128i tkey1 = _mm_set_epi64x(0,k);
	__m128i tkey2 = precompReduction64_si128(_mm_clmulepi64_si128( tkey1, tkey1, 0x00));
	__m128i key = _mm_xor_si128(_mm_and_si128(tkey1,_mm_set_epi64x(0,-1)),_mm_slli_si128(tkey2,8));
	__m128i acc = _mm_set_epi64x(0,value1);
	__m128i temp = _mm_set_epi64x(value2,value3);
	acc = _mm_clmulepi64_si128( acc, key, 0x10);
	const __m128i clprod1 = _mm_clmulepi64_si128( temp, key, 0x01);
	acc = _mm_xor_si128 (clprod1,acc);
    __m128i mask = _mm_set_epi64x(0,-1);
	acc = _mm_xor_si128 (acc,_mm_and_si128 (temp,mask));
	acc = precompReduction64_si128(acc);
	return _mm_cvtsi128_si64(acc);
}

void hornerrule() {
	printf("Testing Horner rules...\n");
	uint64_t key = 0xF0F0F0F0F0F0F0F0UL;    // randomly chosen
	for(int k = 0; k<64; k+=5) {
		uint64_t value1 = (1UL<<k);
		for(int kk = 0; kk<64; kk+=5) {
			uint64_t value2 = (1UL<<kk);
			for(int kkk = 0; kkk<64; kkk+=5) {
				uint64_t value3 = (1UL<<kkk);
				uint64_t l1 = basichorner(key,value1,value2,value3);
				uint64_t l2 = fasthorner(key,value1,value2,value3);
				if(l1 != l2) {
					printf("bug %" PRIu64 " %" PRIu64 " \n",l1,l2);
					abort();
				}
				uint64_t l3 = fasthorner2(key,value1,value2,value3);
				if(l1 != l2) {
					printf("bug23 %" PRIu64 " %" PRIu64 "  \n",l2,l3);
					abort();
				}
			}

		}
	}

}

void clmulunittests() {
	printf("Testing CLMUL code...\n");
	//displayfirst();
	hornerrule();
	precompclmulunittest0_64();
	clmulunittest0_64();
	clmulunittest0_32();
	clmulunittest0_16();
	clmulunittest1();
	clmulunittest2();
	clmulunittest3();
	clmulunittest3a();
	printf("CLMUL code looks ok.\n");
}

void clhashtest() {
    const int N = 1024;
	char * array  = (char*)malloc(N);
	char *  rs = (char*)malloc(N);
	for(int k = 0; k<N; ++k) {
	  array[k] = 0;
	  rs[k] = k+1;
	}
	printf("[clhashtest] initial sanity checking \n");
	{
	  init_clhash(0);
	  uint64_t val1 = 18427963401415413538ULL;
	  uint64_t b1 = clhash(&val1, 8);
	  uint64_t val2 = 18427963401415413539ULL;
	  uint64_t b2 = clhash(&val2, 8);
	  uint64_t b3 = clhash(&val1, 8);
	  assert(b1 == b3);
	  assert(b1 != b2);
	}
	printf("[clhashtest] checking that __clmulhalfscalarproductwithoutreduction agrees with __clmulhalfscalarproductwithtailwithoutreduction\n");
	for(int k = -1; k < 1024; ++k ) {
		char * farray = (char*)malloc(N);
	    for(int kk = 0; kk<1024; ++kk) {
				  farray[kk] = 0;
		}

	    if(k>=0) farray[k] = 1;
	    __m128i acc = __clmulhalfscalarproductwithoutreduction((const __m128i *)rs, (const uint64_t *) farray, 128);
		__m128i acc2 = __clmulhalfscalarproductwithtailwithoutreduction(
				(const __m128i *)rs, (const uint64_t *) farray, 128);
		assert( _mm_extract_epi64(acc,0) == _mm_extract_epi64(acc2,0));
		assert( _mm_extract_epi64(acc,1) == _mm_extract_epi64(acc2,1));
	}
	printf("[clhashtest] checking that flipping a bit changes hash value with CLHASH \n");
	for(int bit = 0; bit < 64; ++bit ) {
			uint64_t x = 0;
			uint64_t orig = CLHASH(rs, &x, 1);
			x ^= ((uint64_t)1) << bit;
			uint64_t flip = CLHASH(rs, &x, 1);
			assert(flip != orig);
			x ^= ((uint64_t)1) << bit;
			uint64_t back = CLHASH(rs, &x, 1);
			assert(back == orig);

	}
	printf("[clhashtest] checking that flipping a bit changes hash value with CLHASHbyte \n");
	for(int bit = 0; bit < 64; ++bit ) {
		for(int length = (bit+8)/8; length <= (int)sizeof(uint64_t); ++length) {
			uint64_t x = 0;
			uint64_t orig = CLHASHbyte(rs, (const char *)&x, length);
			x ^= ((uint64_t)1) << bit;
			uint64_t flip = CLHASHbyte(rs, (const char *)&x, length);
			assert(flip != orig);
			x ^= ((uint64_t)1) << bit;
			uint64_t back = CLHASHbyte(rs, (const char *)&x, length);
			assert(back == orig);
		}
	}

	printf("[clhashtest] checking that CLHASHbyte agrees with CLHASH\n");
	for(int k = 0; k <= 2048; ++k ) {
	    uint64_t * farray = (uint64_t*)malloc(k * sizeof(uint64_t));
		for(int kk = 0; kk<k; ++kk) {
		  farray[kk] = 1024+k;
		}
	    uint64_t hashbyte = CLHASHbyte(rs, (const char *)farray, k*sizeof(uint64_t));
	    uint64_t hashword = CLHASH(rs, farray, k);
	    assert(hashbyte == hashword);
		free(farray);
	}

	free(array);
	free(rs);

}


void lazymod128test() {
	printf("[lazymod] test\n");
	// we know what lazymod has to be for 1-bit inputs, so let us run through it.
	for(int b = 0; b <128; ++b) {
		// in such cases, lazymod should do nothing
		__m128i low = unibit(b);
		__m128i high = _mm_setzero_si128();
		__m128i r = lazymod127(low, high) ;
		assert(equal(r,low));
	}
	for(int b = 0; b <126; ++b) {
		__m128i high = unibit(b);
		__m128i low = _mm_setzero_si128();
		__m128i r = lazymod127(low, high) ;
		__m128i expected = _mm_xor_si128(unibit(b+1), unibit(b+2));
		assert(equal(r,expected));
	}
	for(int b1 = 1; b1 <126; ++b1) {
		for(int b2 = 0; b2 <b1; ++b2) {
		  __m128i high = _mm_xor_si128(unibit(b1),unibit(b2));
		  __m128i low = _mm_setzero_si128();
		  __m128i r = lazymod127(low, high) ;
		  __m128i expected1 = _mm_xor_si128(unibit(b1+1), unibit(b1+2));
		  __m128i expected2 = _mm_xor_si128(unibit(b2+1), unibit(b2+2));
		  __m128i expected = _mm_xor_si128(expected1, expected2);
		  assert(equal(r,expected));
		}
	}
	printf("[lazymod] ok\n");

}

void clhashsanity() {
	printf("Checking if clhash remains unchanged  \n ");

	uint64_t * keys  = (uint64_t*)malloc(RANDOM_64BITWORDS_NEEDED_FOR_CLHASH*sizeof(uint64_t));
	for(int k = 0; k < RANDOM_64BITWORDS_NEEDED_FOR_CLHASH; ++k) {
	   keys[k] = ~ (3*k) ;
	}
	int N = 1000000;
	uint64_t * data  = (uint64_t*)malloc(N*sizeof(uint64_t));
	for(int k = 0; k < N; ++k) {
	   data[k] = k ;
	}
	uint64_t r1 = CLHASH(keys, data,3);
	printf("length 3 word %llu  \n ", r1);
	assert(r1 == 1514759017041891781ULL);
	uint64_t r2 = CLHASHbyte(keys, (const char*)data,3*8);
	printf("length 3 word %llu  \n ", r2);
	assert(r2 == 1514759017041891781ULL);
	uint64_t r3 = CLHASH(keys, data,N);
	printf("length N word %llu  \n ", r3);
	assert(r3 == 10692194684003262447ULL);
	uint64_t r4 = CLHASHbyte(keys, (const char*)data,N*8);
	printf("length N word %llu  \n ", r4);
	assert(r4 == 10692194684003262447ULL);
	free(keys);
	free(data);
	printf("Ok \n");

}

int main() {
	clhashsanity();
	lazymod128test();
	clhashtest();
	clmulunittests();
	return 0;
}

#else
int main() {
	printf("clmul instruction set not detected. Testing not done.");
	return 0;
}

#endif



////////////////////////////////////////////////////////////////
//Did you know that...
//
// 63,1,0
//
//
// 127,1,0
//
// are irreducible polynomials?
////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////
// Rest is garbage.
//
//This uses 128,7,2,1,0.
// and the formula
//(((A div x^n) * M ) div x^n) * M) mod x^n
//+(A mod x^n)
//
// It assumes that Ahigh has its two highest bits to zero.
// It does not actually do a full reduction, merely returning
// reduced 128 bits... if the highest bit is set, further work
// could be needed.
//
//
////////////////////////////////////////////////////////////////
//__m128i barrettWithoutPrecomputation128( __m128i Alow, __m128i Ahigh) {
//	const __m128i C = _mm_set_epi64x(0,(1<<7)+(1<<2)+(1<<1)+1);// C is the lower 128 bits of the irreducible poly.
// we have that (A div x^n) is just Ahigh
// to compute (Ahigh * M) div x^n, we just need the high part from C[0] times Ahigh[1] + Ahigh
//	__m128i firstmult = _mm_xor_si128(_mm_srli_si128(_mm_clmulepi64_si128(Ahigh,C,0x10),8),Ahigh);
// C0 * f0 + c0 * f1 [0]
//	__m128i secondmult = _mm_clmulepi64_si128(C,firstmult,0x00),_mm_clmulepi64_si128(C,firstmult,0x01)

//	__m128i s1 = _mm_slli_si128(Ahigh,1);
//	__m128i s2 = _mm_slli_si128(Ahigh,2);
//	return _mm_xor_si128(_mm_xor_si128(s1,s2),Alow);
//}

