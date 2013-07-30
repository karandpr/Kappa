/* ---------------------------------------------------- *
 * oneiteration.h -- Linux kernel module for HAVEGE     *
 * ---------------------------------------------------- *
 * Copyright (c) 2006 - O. Rochecouste, A. Seznec       *
 *                                                      *
 * based on HAVEGE 2.0 source code by: A. Seznec        *
 *                                                      *
 * This library is free software; you can redistribute  *
 * it and/or  * modify it under the terms of the GNU    *
 * Lesser General Public License as published by the    *
 * Free Software Foundation; either version 2.1 of the  *
 * License, or (at your option) any later version.      *
 *                                                      *
 * This library is distributed in the hope that it will *
 * be useful, but WITHOUT ANY WARRANTY; without even the*
 * implied warranty of MERCHANTABILITY or FITNESS FOR A *
 * PARTICULAR PURPOSE. See the GNU Lesser General Public*
 * License for more details.                            *
 *                                                      *
 * You should have received a copy of the GNU Lesser    *
 * General Public License along with this library; if   *
 * not, write to the Free Software Foundation, Inc., 51 *
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301 *
 * USA                                                  *
 *                                                      *
 * contact information: orocheco [at] irisa [dot] fr    *
 * ==================================================== */

#ifndef HRANDOM_EOF

/* ---------------------------------------------------- *
 * On average, one iteration accesses two 8-word blocks *
 * in the havege_walk table, and generates 16 words in  *
 * the result array.                                    *
 *                                                      *
 * Data read from the havege_walk table are updated and *
 * permuted after each use.                             *
 *                                                      *
 * The value of the hardware clock counter is used for  * 
 * this update.                                         *
 *                                                      *
 * 21 conditional tests are present along with two asm  *
 * calls for accessing the hardware clock counter. The  *
 * conditional tests are grouped in two nested  groups  *
 * of 10 conditional tests and 1 test that controls the *
 * permutation.                                         *
 *                                                      *
 * In average, there should be 4 tests executed and, in *
 * average, 2 of them should be mispredicted.           *
 * ==================================================== */

{
  PTtest = state->PT >> 20;

  if (PTtest & 1) {
    PTtest ^= 3; PTtest = PTtest >> 1;
    if (PTtest & 1) {
      PTtest ^= 3; PTtest = PTtest >> 1;
      if (PTtest & 1) {
	PTtest ^= 3; PTtest = PTtest >> 1;
	if (PTtest & 1) {
	  PTtest ^= 3; PTtest = PTtest >> 1;
	  if (PTtest & 1) {
	    PTtest ^= 3; PTtest = PTtest >> 1;
	    if (PTtest & 1) {
	      PTtest ^= 3; PTtest = PTtest >> 1;
	      if (PTtest & 1) {
		PTtest ^= 3; PTtest = PTtest >> 1;
		if (PTtest & 1) {
		  PTtest ^= 3; PTtest = PTtest >> 1;
		  if (PTtest & 1) {
		    PTtest ^= 3; PTtest = PTtest >> 1;
		    if (PTtest & 1) {
		      PTtest ^= 3; PTtest = PTtest >> 1;
		    }
		  }
		}
	      }
	    }
	  }
	}
      }
    }
  };

  PTtest = PTtest >> 1;
  state->pt = (state->PT >> 18) & 7;

  state->PT = state->PT & HRANDOM_ANDPT;

  HARDCLOCK(state->rdtsc);

  Pt0 = &state->walkp[state->PT];
  Pt1 = &state->walkp[state->PT2];
  Pt2 = &state->walkp[state->PT  ^ 1];
  Pt3 = &state->walkp[state->PT2 ^ 4];

  result[i] ^= *Pt0;
  result[i+1] ^= *Pt1;
  result[i+2] ^= *Pt2;
  result[i+3] ^= *Pt3;

  i += 4;

  inter = (*Pt0 >> (1)) ^ (*Pt0 << (31)) ^ state->rdtsc;
  *Pt0  = (*Pt1 >> (2)) ^ (*Pt1 << (30)) ^ state->rdtsc;
  *Pt1  = inter;
  *Pt2  = (*Pt2 >> (3)) ^ (*Pt2 << (29)) ^ state->rdtsc;
  *Pt3  = (*Pt3 >> (4)) ^ (*Pt3 << (28)) ^ state->rdtsc;

  Pt0 = &state->walkp[state->PT  ^ 2];
  Pt1 = &state->walkp[state->PT2 ^ 2];
  Pt2 = &state->walkp[state->PT  ^ 3];
  Pt3 = &state->walkp[state->PT2 ^ 6];

  result[i] ^= *Pt0;
  result[i+1] ^= *Pt1;
  result[i+2] ^= *Pt2;
  result[i+3] ^= *Pt3;

  i += 4;

  if (PTtest & 1) {
    int *Ptinter;
    Ptinter = Pt0;
    Pt2 = Pt0;
    Pt0 = Ptinter;
  }

  PTtest = (state->PT2 >> 18);
  inter  = (*Pt0 >> (5)) ^ (*Pt0 << (27)) ^ state->rdtsc;
  *Pt0   = (*Pt1 >> (6)) ^ (*Pt1 << (26)) ^ state->rdtsc;
  *Pt1   = inter;

  HARDCLOCK(state->rdtsc);

  *Pt2 = (*Pt2 >> (7)) ^ (*Pt2 << (25)) ^ state->rdtsc;
  *Pt3 = (*Pt3 >> (8)) ^ (*Pt3 << (24)) ^ state->rdtsc;

  Pt0 = &state->walkp[state->PT  ^ 4];
  Pt1 = &state->walkp[state->PT2 ^ 1];

  state->PT2 = (result[(i - 8) ^ state->pt2] ^
		state->walkp[state->PT2 ^ state->pt2 ^ 7]);

  state->PT2 = ((state->PT2 & HRANDOM_ANDPT) & (0xfffffff7))
    ^ ((state->PT ^ 8) & 0x8);

  /* avoid state->PT and state->PT2 to point on the same *
     cache block                                         */
  state->pt2 = ((state->PT2 >> 28) & 7);

  if (PTtest & 1) {
    PTtest ^= 3; PTtest = PTtest >> 1;
    if (PTtest & 1) {
      PTtest ^= 3; PTtest = PTtest >> 1;
      if (PTtest & 1) {
	PTtest ^= 3; PTtest = PTtest >> 1;
	if (PTtest & 1) {
	  PTtest ^= 3; PTtest = PTtest >> 1;
	  if (PTtest & 1) {
	    PTtest ^= 3; PTtest = PTtest >> 1;
	    if (PTtest & 1) {
	      PTtest ^= 3; PTtest = PTtest >> 1;
	      if (PTtest & 1) {
		PTtest ^= 3; PTtest = PTtest >> 1;
		if (PTtest & 1) {
		  PTtest ^= 3; PTtest = PTtest >> 1;
		  if (PTtest & 1) {
		    PTtest ^= 3; PTtest = PTtest >> 1;
		    if (PTtest & 1) {
		      PTtest ^= 3; PTtest = PTtest >> 1;
		    }
		  }
		}
	      }
	    }
	  }
	}
      }
    }
  };

  Pt2 = &state->walkp[state->PT  ^ 5];
  Pt3 = &state->walkp[state->PT2 ^ 5];

  result[i] ^= *Pt0;
  result[i+1] ^= *Pt1;
  result[i+2] ^= *Pt2;
  result[i+3] ^= *Pt3;

  i += 4;

  inter = (*Pt0 >> (9))  ^ (*Pt0 << (23)) ^ state->rdtsc;
  *Pt0  = (*Pt1 >> (10)) ^ (*Pt1 << (22)) ^ state->rdtsc;
  *Pt1  = inter;
  *Pt2  = (*Pt2 >> (11)) ^ (*Pt2 << (21)) ^ state->rdtsc;
  *Pt3  = (*Pt3 >> (12)) ^ (*Pt3 << (20)) ^ state->rdtsc;

  Pt0 = &state->walkp[state->PT  ^ 6];
  Pt1 = &state->walkp[state->PT2 ^ 3];
  Pt2 = &state->walkp[state->PT  ^ 7];
  Pt3 = &state->walkp[state->PT2 ^ 7];

  result[i] ^= *Pt0;
  result[i+1] ^= *Pt1;
  result[i+2] ^= *Pt2;
  result[i+3] ^= *Pt3;

  i += 4;

  inter = (*Pt0 >> (13)) ^ (*Pt0 << (19)) ^ state->rdtsc;
  *Pt0  = (*Pt1 >> (14)) ^ (*Pt1 << (18)) ^ state->rdtsc;
  *Pt1  = inter;
  *Pt2  = (*Pt2 >> (15)) ^ (*Pt2 << (17)) ^ state->rdtsc;
  *Pt3  = (*Pt3 >> (16)) ^ (*Pt3 << (16)) ^ state->rdtsc;

  /* avoid state->PT and state->PT2 to point on the same *
   * cache block                                         */
  state->PT = (((result[(i - 8) ^ state->pt] ^
		 state->walkp[state->PT ^ state->pt ^ 7]))
	       & (0xffffffef)) ^ ((state->PT2 ^ 0x10) & 0x10);
}

#endif /* HRANDOM_EOF */
