/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *\
 * This is GNU GO, a Go program. Contact gnugo@gnu.org, or see   *
 * http://www.gnu.org/software/gnugo/ for more information.      *
 *                                                               *
 * Copyright 1999, 2000, 2001 by the Free Software Foundation.   *
 *                                                               *
 * This program is free software; you can redistribute it and/or *
 * modify it under the terms of the GNU General Public License   *
 * as published by the Free Software Foundation - version 2.     *
 *                                                               *
 * This program is distributed in the hope that it will be       *
 * useful, but WITHOUT ANY WARRANTY; without even the implied    *
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR       *
 * PURPOSE.  See the GNU General Public License in file COPYING  *
 * for more details.                                             *
 *                                                               *
 * You should have received a copy of the GNU General Public     *
 * License along with this program; if not, write to the Free    *
 * Software Foundation, Inc., 59 Temple Place - Suite 330,       *
 * Boston, MA 02111, USA.                                        *
\* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "liberty.h"
#include "patterns.h"

static void compute_effective_worm_sizes(void);
static void compute_unconditional_status(void);
static void find_worm_attacks_and_defenses(void);
static void find_worm_threats(void);
static int find_lunch(int str, int *lunch);
static int genus(int str);
static void markcomponent(int str, int pos, int mg[BOARDMAX]);
static int examine_cavity(int pos, int *edge);
static void cavity_recurse(int pos, int mx[BOARDMAX], 
			   int *border_color, int *edge, int str);
static void ping_cave(int str, int *result1,  int *result2,
		      int *result3, int *result4);
static void ping_recurse(int pos, int *counter, 
			 int mx[BOARDMAX], 
			 int mr[BOARDMAX], int color);
static int touching(int pos, int color);
static void find_attack_patterns(void);
static void attack_callback(int m, int n, int color,
			    struct pattern *pattern, int ll, void *data);
static void find_defense_patterns(void);
static void defense_callback(int m, int n, int color,
			     struct pattern *pattern, int ll, void *data);


/* A STRING is a maximal connected set of stones of the same color, 
 * black or white. A WORM is the same thing as a string, except that
 * its color can be empty. An empty worm is called a CAVITY.
 *
 * Worms are eventually amalgamated into dragons. An empty dragon
 * is called a CAVE.
 */



/* make_worms() finds all worms and assembles some data about them.
 *
 * Each worm is marked with an origin, having coordinates (origini, originj).
 * This is an arbitrarily chosen element of the worm, in practice the
 * algorithm puts the origin at the first element when they are given
 * the lexicographical order, though its location is irrelevant for
 * applications. To see if two stones lie in the same worm, compare
 * their origins.
 *
 * We will use the field dragon[m][n].genus to keep track of
 * black- or white-bordered cavities (essentially eyes) which are found.  
 * so this field must be zero'd now.
 */

void
make_worms(void)
{
  int m,n; /* iterate over board */

  /* Build the basic worm data:  color, origin, size, liberties. */
  build_worms();

  /* No point continuing if the board is completely empty. */
  if (stones_on_board(BLACK | WHITE) == 0)
    return;

  /* Compute effective sizes of all worms. */
  compute_effective_worm_sizes();

  /* Look for unconditionally alive and dead worms, and unconditional
   * territory.
   */
  compute_unconditional_status();
  
  find_worm_attacks_and_defenses();
  
  gg_assert(stackp == 0);

  /* Count liberties of different orders and initialize cutstone fields. */
  
  for (m = 0; m < board_size; m++)
    for (n = 0; n < board_size; n++) {
      int pos = POS(m, n);
      if (board[pos] && is_worm_origin(pos, pos)) {
	int lib1, lib2, lib3, lib4;

	ping_cave(pos, &lib1, &lib2, &lib3, &lib4);
	gg_assert(worm[pos].liberties == lib1);
	worm[pos].liberties2 = lib2;
	worm[pos].liberties3 = lib3;
	worm[pos].liberties4 = lib4;
	worm[pos].cutstone = 0;
	worm[pos].cutstone2 = 0;
	propagate_worm(pos);
      }
    }
  
  gg_assert(stackp == 0);

/*
 * There are two concepts of cutting stones in the worm array.
 *
 * worm.cutstone:
 *
 *     A CUTTING STONE is one adjacent to two enemy strings,
 *     which do not have a liberty in common. The most common
 *     type of cutting string is in this situation.
 *  
 *     XO
 *     OX
 *     
 *     A POTENTIAL CUTTING STONE is adjacent to two enemy
 *     strings which do share a liberty. For example, X in:
 *     
 *     XO
 *     O.
 *     
 *     For cutting strings we set worm[m][n].cutstone=2. For potential
 *     cutting strings we set worm[m][n].cutstone=1. For other strings,
 *     worm[m][n].cutstone=0.
 *
 * worm.cutstone2:
 *
 *     Cutting points are identified by the patterns in the
 *     connections database. Proper cuts are handled by the fact
 *     that attacking and defending moves also count as moves
 *     cutting or connecting the surrounding dragons. 
 *
 * The cutstone field will now be set. The cutstone2 field is set
 * later, during find_cuts(), called from make_domains().
 *
 * We maintain both fields because the historically older cutstone
 * field is needed to deal with the fact that e.g. in the position
 *
 *
 *    OXX.O
 *    .OOXO
 *    OXX.O
 *
 * the X stones are amalgamated into one dragon because neither cut
 * works as long as the two O stones are in atari. Therefore we add
 * one to the cutstone field for each potential cutting point,
 * indicating that these O stones are indeed worth rescuing.
 *
 * For the time being we use both concepts in parallel. It's
 * possible we also need the old concept for correct handling of lunches.
 */

  for (m = 0; m < board_size; m++)
    for (n = 0; n < board_size; n++) {
      int pos = POS(m, n);
      int w1 = NO_MOVE;
      int w2 = NO_MOVE;
      int i, j, k, di, dj;

      /* Only work on each worm once. This is easiest done if we only 
       * work with the origin of each worm.
       */
      if (board[pos] == EMPTY || !is_worm_origin(pos, pos))
	continue;

      /* Try to find two adjacent worms (ui,uj) and (ti,tj) 
       * of opposite colour from (m, n).
       */
      for (i = 0; i < board_size; i++)
	for (j = 0; j < board_size; j++) {

	  /* Work only with the opposite color from (m, n). */
	  if (BOARD(i, j) != OTHER_COLOR(board[pos])) 
	    continue;
	      
	  for (k = 0; k < 4; k++) {
	    di = i + deltai[k];
	    dj = j + deltaj[k];
	    if (ON_BOARD2(di, dj) && is_worm_origin(POS(di, dj), pos)) {

	      ASSERT2(BOARD(di, dj) == board[pos], m, n);

	    /* If we have not already found a worm which meets the criteria,
	     * store it into (ti, tj), otherwise store it into (ui, uj).
	     */
	      if (w1 == NO_MOVE)
	        w1 = worm[POS(i, j)].origin;
	      else if (!is_same_worm(POS(i, j), w1))
	        w2 = worm[POS(i, j)].origin;
	    }
	  } /* loop over k */
	} /* loop over i,j */

      /* 
       *  We now verify the definition of cutting stones. We have
       *  verified that the string at (m,n) is adjacent to two enemy
       *  strings at (ti,tj) and (ui,uj). We need to know if these
       *  strings share a liberty.
       */

      /* Only do this if we really found anything. */
      if (w2 != NO_MOVE) {
	int lib;  /* look for a common liberty vi,vj */
	TRACE("Worm at %1m has w1 %1m and w2 %1m\n", pos, w1, w2);
        worm[pos].cutstone = 2;
	for (lib = BOARDMIN; lib < BOARDMAX; lib++) {
	  if (board[lib] != EMPTY) 
	    continue;
	  if (liberty_of_string(lib, w1) && liberty_of_string(lib, w2))
	    worm[pos].cutstone = 1;
	  
	  DEBUG(DEBUG_WORMS,
		"Worm at %1m has w1 %1m and w2 %1m, cutstone %d\n",
		pos, w1, w2, worm[pos].cutstone);
	}
      }

    } /* loop over m,n */

  gg_assert(stackp == 0);
  
  /* Set the genus of all worms. */
  for (m = 0; m < board_size; m++)
    for (n = 0; n < board_size; n++) {
      int pos = POS(m, n);
      if (board[pos] && is_worm_origin(pos, pos)) {
 	worm[pos].genus = genus(pos);
	propagate_worm(pos);
      }
    }
  gg_assert(stackp == 0);

  /* We try first to resolve small semeais. */
  small_semeai();
  gg_assert(stackp == 0);

  /* Now we try to improve the values of worm.attack and worm.defend. If we
   * find that capturing the string at (m,n) also defends the string at (i,j),
   * or attacks it, then we move the point of attack and defense.
   * We don't move the attacking point of strings that can't be defended.
   */
  {
    int mi[BOARDMAX]; /* mark changed information */
    int mxcolor[BOARDMAX]; /* mark tried moves for color */
    int mxother[BOARDMAX]; /* mark tried moves for other */
    int i, j;

    memset(mi, 0, sizeof(mi));
    memset(mxcolor, 0, sizeof(mi));
    memset(mxother, 0, sizeof(mi));
    
    for (m = 0; m < board_size; m++)
      for (n = 0; n < board_size; n++) {
	int pos = POS(m, n);
	
	int color = board[pos];
	int other = OTHER_COLOR(color);
	
	int aa = worm[pos].attack_point;
	int dd = worm[pos].defense_point;
	
	/* For each worm, only work with the origin. */
	if (board[pos] == EMPTY || !is_worm_origin(pos, pos))
	  continue;
	
	/* If the opponent has an attack on the worm (m, n), and we
	 * have not tried this move before, carry it out and see
	 * what it leads to.
	 */
	if (aa != NO_MOVE && mxother[aa] == 0) {
	  
	  mxother[aa] = 1;
	  /* First, carry out the attacking move. */
	  if (trymove(aa, other, "make_worms", NO_MOVE, EMPTY, NO_MOVE)) {
	    
	    /* We must read to the same depth that was used in the
	     * initial determination of worm.attack and worm.defend
	     * to avoid horizon effect. Since stackp has been
	     * incremented we must also increment depth and
	     * backfill_depth. */
	    
	    /* Now we try to find a group which is saved or attacked as well
	       by this move. */
	    TRACE("trying %1m\n", aa);
	    increase_depth_values();
	    
	    for (i = 0; i < board_size; i++)
	      for (j = 0; j < board_size; j++) {
		int pos2 = POS(i, j);
		/* If a worm has its origin (i, j), and it's not (m, n)...*/
		if (board[pos2]
		    && is_worm_origin(pos2, pos2)
		    && (pos2 != pos)) {
		  
		  /* Either the worm is of the same color as (m, n),
		   * then we try to attack it.  If there was a previous 
		   * attack and defense of it, and there is no defence
		   * for the attack now...
		   */
		  if (worm[pos2].color == color
		      && worm[pos2].attack_code != 0
		      && worm[pos2].defend_code != 0
		      && !find_defense(pos2, NULL)) {
		    
		    int attack_works = 1;
		    /* Sometimes find_defense() fails to find a
		     * defense which has been found by other means.
		     * Try if the old defense move still works.
		     */
		    if (worm[pos2].defense_point != 0
			&& trymove(worm[pos2].defense_point,
				   color, "make_worms", 0, EMPTY, 0)) {
		      if (!attack(pos2, NULL))
			attack_works = 0;
		      popgo();
		    }
		    
		    /* ...then move the attack point of that worm to
		     * the attack point of (m, n).
		     */
		    if (attack_works) {
		      TRACE("moving point of attack of %1m to %1m\n",
			    pos2, aa);
		      worm[pos2].attack_point = aa;
		      add_attack_move(aa, pos2);
		      mi[pos2] = 1;
		    }
		  }
		  /* Or the worm is of the opposite color as (m, n).
		   * If there previously was an attack on it, but there
		   * is none now, then move the defence point of (i, j)
		   * to the defence point of (m, n).
		   */
		  else if (worm[pos2].color == other
			   && worm[pos2].attack_code != 0
			   && !attack(pos2, NULL)) {
		    if (worm[pos2].defend_code != 0)
		      TRACE("moving point of defense of %1m to %1m\n",
			    pos2, dd);
		    else
		      TRACE("setting point of defense of %1m to %1m\n",
			    pos2, dd);
		    worm[pos2].defend_code   = WIN;
		    worm[pos2].defense_point = aa;
		    add_defense_move(aa, pos2);
		    mi[pos2] = 1;
		  }
		}
	      }
	    popgo();
	    decrease_depth_values();
	  }
	}
	
	/* If there is a defense point for the worm (m, n), and we
	 * have not tried this move before, move there and see what
	 * it leads to.
	 */
	if (dd != NO_MOVE && mxcolor[dd] == 0) {

	  mxcolor[dd] = 1;
	  /* First carry out the defending move. */
	  if (trymove(dd, color, "make_worms", NO_MOVE, EMPTY, NO_MOVE)) {
	    
	    TRACE("trying %1m\n", dd);
	    increase_depth_values();
	    
	    for (i = 0; i < board_size; i++)
	      for (j = 0; j < board_size; j++) {
		int pos2 = POS(i, j);
		
		/* If a worm has its origin (i, j), and it's not (m, n)...*/
		if (board[pos2]
		    && is_worm_origin(pos2, pos2)
		    && (pos2 != pos)) {
		  
		  /* Either the worm is of the opposite color as (m, n),
		   * then we try to attack it.  If there was a previous 
		   * attack and defense of it, and there is no defence
		   * for the attack now...
		   */
		  if (worm[pos2].color == other
		      && worm[pos2].attack_code != 0 
		      && worm[pos2].defend_code != 0
		      && !find_defense(pos2, NULL)) {
		    
		    int attack_works = 1;
		    /* Sometimes find_defense() fails to find a
		       defense which has been found by other means.
		       Try if the old defense move still works. */
		    if (trymove(worm[pos2].defense_point, other, "make_worms",
				NO_MOVE, EMPTY, NO_MOVE)) {
		      if (!attack(pos2, NULL))
			attack_works = 0;
		      popgo();
		    }
		      
		    /* ...then move the attack point of that worm to
		     * the defense point of (m, n).
		     */
		    if (attack_works) {
		      TRACE("moving point of attack of %1m to %1m\n",
			    pos2, dd);
		      worm[pos2].attack_point = dd;
		      add_attack_move(dd, POS(i, j));
		      mi[pos2] = 1;
		    }
		  }
		  /* Or the worm is of the same color as (m, n).
		   * If there previously was an attack on it, but there
		   * is none now, then move the defence point of (i, j)
		   * to the defence point of (m, n).
		   */
		  else if (worm[pos2].color == color
			   && worm[pos2].attack_code != 0
			   && !attack(pos2, NULL)) {
		    if (worm[pos2].defend_code != 0)
		      TRACE("moving point of defense of %1m to %1m\n",
			    pos2, dd);
		    else
		      TRACE("setting point of defense of %1m to %1m\n",
			    pos2, dd);
		    worm[pos2].defend_code   = WIN;
		    worm[pos2].defense_point = dd;
		    add_defense_move(dd, pos2);
		    mi[pos2] = 1;
		  }
		}
	      }
	    popgo();
	    decrease_depth_values();
	  }
	}
      }
    
    /* Propagate the newly generated info to all other stones of each worm. */
    for (i = 0; i < board_size; i++)
      for (j = 0; j < board_size; j++)
	if (mi[POS(i, j)])
	  propagate_worm(POS(i, j));
  }

  gg_assert(stackp == 0);
  
  /* Sometimes it happens that the tactical reading finds adjacent
   * strings which both can be attacked but not defended. (The reason
   * seems to be that the attacker tries harder to attack a string,
   * than the defender tries to capture it's neighbors.) When this
   * happens, the eyes code produces overlapping eye spaces and still
   * worse all the nondefendable stones actually get amalgamated with
   * their allies on the outside.
   *
   * To solve this we scan through the strings which can't be defended
   * and check whether they have a neighbor that can be attacked. In
   * this case we set the defense point of the former string to the
   * attacking point of the latter.
   *
   * Please notice that find_defense() will still read this out
   * incorrectly, which may lead to some confusion later.
   */

  /* First look for vertical neighbors. */
  for (m = 0; m < board_size-1; m++)
    for (n = 0; n < board_size; n++) {
      int pos = POS(m, n);
      if (!is_same_worm(pos, SOUTH(pos))
	  && board[pos] != EMPTY
	  && board[SOUTH(pos)] != EMPTY) {
        if (worm[pos].attack_code != 0 && worm[SOUTH(pos)].attack_code != 0) {
	  if (worm[pos].defend_code == 0
	      && does_defend(worm[SOUTH(pos)].attack_point, pos)) {
	    /* FIXME: need to check ko relationship here */
	    change_defense(pos, worm[SOUTH(pos)].attack_point, WIN);
	  }
	  if (worm[SOUTH(pos)].defend_code == 0
              && does_defend(worm[pos].attack_point, SOUTH(pos))) {
	    /* FIXME: need to check ko relationship here */	    
	    change_defense(SOUTH(pos), worm[pos].attack_point, WIN);
	  }
        }
      }
    }
  
  /* Then look for horizontal neighbors. */
  for (m = 0; m < board_size; m++)
    for (n = 0; n < board_size-1; n++) {
      int pos = POS(m, n);
      if (!is_same_worm(pos, EAST(pos))
	  && board[pos] != EMPTY
	  && board[EAST(pos)] != EMPTY) {
        if (worm[pos].attack_code != 0 && worm[EAST(pos)].attack_code != 0) {
	  if (worm[pos].defend_code == 0
              && does_defend(worm[EAST(pos)].attack_point, pos)) {
	    /* FIXME: need to check ko relationship here */	    
	    change_defense(pos, worm[EAST(pos)].attack_point, WIN);
	  }
	  if (worm[EAST(pos)].defend_code == 0
              && does_defend(worm[pos].attack_point, EAST(pos))) {
	    /* FIXME: need to check ko relationship here */	    
	    change_defense(EAST(pos), worm[pos].attack_point, WIN);
	  }
	}
      }
    }
  
  gg_assert(stackp == 0);
  
  /* Find adjacent worms that can be easily captured, aka lunches. */

  for (m = 0; m < board_size; m++)
    for (n = 0; n < board_size; n++) {
      int pos = POS(m, n);
      int lunch;

      if (board[pos] == EMPTY || !is_worm_origin(pos, pos))
	continue;

      if (find_lunch(pos, &lunch)
	  && (worm[lunch].attack_code == WIN
	      || worm[lunch].attack_code == KO_A)) {
	TRACE("lunch found for %1m at %1m\n", pos, lunch);
	worm[pos].lunch = lunch;
      }
      else
	worm[pos].lunch = NO_MOVE;

      propagate_worm(pos);
    }
  
  if (!disable_threat_computation)
    find_worm_threats();

  /* Identify INESSENTIAL strings.
   *
   * These are defined as surrounded strings which have no life
   * potential unless part of their surrounding chain can be captured.
   * We give a conservative definition of inessential:
   *  - the genus must be zero 
   *  - there can no second order liberties
   *  - there can be no more than two edge liberties
   *  - if it is removed from the board, the remaining cavity has
   *    border color the opposite color of the string 
   *  - it contains at most two edge vertices.
   *
   * If we get serious about identifying seki, we might want to add:
   *
   *  - if it has fewer than 4 liberties it is tactically dead.
   *
   * The last condition is helpful in excluding strings which are
   * alive in seki.
   *
   * An inessential string can be thought of as residing inside the
   * opponent's eye space.
   */

  for (m = 0; m < board_size; m++)
    for (n = 0; n < board_size; n++) {
      int pos = POS(m, n);
      if (board[pos]
	  && worm[pos].origin == pos
	  && worm[pos].genus == 0
	  && worm[pos].liberties2 == 0
	  && worm[pos].lunch == NO_MOVE)
      {
	int edge;

	int border_color = examine_cavity(pos, &edge);
	if (border_color != GRAY_BORDER && edge < 3) {
	  worm[pos].inessential = 1;
	  propagate_worm(pos);
	}
      }
    }
}


/* 
 * Clear all worms and initialize the basic data fields:
 *   color, origin, size, liberties
 * This is a substep of make_worms().
 */

void
build_worms()
{
  int m, n;
  int pos;

  /* Set all worm data fields to 0. */
  memset(worm, 0 , sizeof(worm));

  /* Initialize the worm data for each worm. */
  for (m = 0; m < board_size; m++)
    for (n = 0; n < board_size; n++)
      worm[POS(m, n)].origin = NO_MOVE;

  for (m = 0; m < board_size; m++)
    for (n = 0; n < board_size; n++) {
      pos = POS(m, n);
      if (worm[pos].origin != NO_MOVE)
	continue;
      worm[pos].color = board[pos];
      worm[pos].origin = pos;
      worm[pos].inessential = 0;
      worm[pos].invincible = 0;
      worm[pos].unconditional_status = UNKNOWN;
      worm[pos].effective_size = 0.0;
      if (board[pos] != EMPTY) {
	worm[pos].liberties = countlib(pos);
	worm[pos].size = countstones(pos);
	propagate_worm(pos);
      }
    }
}


/* Compute effective size of each worm. 
 *
 * Effective size is the number of stones in a worm plus half the
 * number of empty intersections that are at least as close to this
 * worm as to any other worm. This is used to estimate the direct
 * territorial value of capturing a worm. Intersections that are
 * shared are counted with equal fractional values for each worm.
 *
 * We never count intersections further away than distance 3.
 */

static void
compute_effective_worm_sizes()
{
  int m, n;

  /* Distance to closest worm, -1 means unassigned, 0 that there is
   * a stone at the location, 1 a liberty of a stone, and so on.
   */
  int distance[MAX_BOARD][MAX_BOARD];
  /* Pointer to the origin of the closest worms. A very large number of
   * worms may potentially be equally close, but no more than
   * 2*(board_size-1).
   */
  static int worms[MAX_BOARD][MAX_BOARD][2*(MAX_BOARD-1)];
  int nworms[MAX_BOARD][MAX_BOARD];   /* number of equally close worms */
  int found_one;
  int dist; /* current distance */
  int k, l;
  int r;
    
  /* Initialize arrays. */
  for (m = 0; m < board_size; m++)
    for (n = 0; n < board_size; n++) {

      for (k = 0; k < 2*(board_size-1); k++)
	worms[m][n][k] = NO_MOVE;
      
      nworms[m][n] = 0;
	
      if (BOARD(m, n) == EMPTY)
	distance[m][n] = -1;
      else {
	distance[m][n] = 0;
	worms[m][n][0] = worm[POS(m, n)].origin;
	nworms[m][n]++;
      }
    }

  dist = 0;
  found_one = 1;
  while (found_one && dist <= 3) {
    found_one = 0;
    dist++;
    for (m = 0; m < board_size; m++)
      for (n = 0; n < board_size; n++) {
	if (distance[m][n] != -1)
	  continue; /* already claimed */

	for (r = 0; r < 4; r++) {
	  int ai = m + deltai[r];
	  int aj = n + deltaj[r];
	  
	  if (ON_BOARD2(ai, aj) && distance[ai][aj] == dist - 1) {
	    found_one = 1;
	    distance[m][n] = dist;
	    for (k = 0; k < nworms[ai][aj]; k++) {
	      int already_counted = 0;
	      for (l = 0; l < nworms[m][n]; l++)
		if (worms[m][n][l] == worms[ai][aj][k]) {
		  already_counted = 1;
		  break;
		}
	      if (!already_counted) {
		gg_assert (nworms[m][n] < 2*(board_size-1));
		worms[m][n][nworms[m][n]] = worms[ai][aj][k];
		nworms[m][n]++;
	      }
	    }
	  }
	}
      }
  }

  /* Distribute (fractional) contributions to the worms. */
  for (m = 0; m < board_size; m++)
    for (n = 0; n < board_size; n++)
      for (k = 0; k < nworms[m][n]; k++) {
	int aa = worms[m][n][k];
	if (BOARD(m, n) == EMPTY)
	  worm[aa].effective_size += 0.5/nworms[m][n];
	else
	  worm[aa].effective_size += 1.0;
      }
	
  /* Propagate the effective size values all over the worms. */
  for (m = 0; m < board_size; m++)
    for (n = 0; n < board_size; n++)
      if (BOARD(m, n) != EMPTY && is_worm_origin(POS(m, n), POS(m, n)))
	propagate_worm(POS(m, n));
}

/* Identify worms which are unconditionally uncapturable in the
 * strongest sense, i.e. even if the opponent is allowed an arbitrary
 * number of consecutive moves. Also identify worms which are
 * similarly unconditionally dead and empty points which are
 * unconditional territory for either player.
 */
static void
compute_unconditional_status()
{
  int unconditional_territory[MAX_BOARD][MAX_BOARD];
  int m, n;
  int color;
  int other;
  
  for (color = WHITE; color <= BLACK; color++) {
    other = OTHER_COLOR(color);
    unconditional_life(unconditional_territory, color);
    for (m = 0; m < board_size; m++)
      for (n = 0; n < board_size; n++) {
	int pos = POS(m, n);
	if (!unconditional_territory[m][n])
	  continue;
	
	if (board[pos] == color) {
	  worm[pos].unconditional_status = ALIVE;
	  if (unconditional_territory[m][n] == 1)
	    worm[pos].invincible = 1;
	}
	else if (board[pos] == EMPTY) {
	  if (color == WHITE)
	    worm[pos].unconditional_status = WHITE_BORDER;
	  else
	    worm[pos].unconditional_status = BLACK_BORDER;
	}
	else
	  worm[pos].unconditional_status = DEAD;
      }
  }
  gg_assert(stackp == 0);
}

/*
 * Analyze tactical safety of each worm. 
 */

static void
find_worm_attacks_and_defenses()
{
  int m, n;

   /* 1. Start with finding attack points. */
  for (m = 0; m < board_size; m++)
    for (n = 0; n < board_size; n++) {
      int pos = POS(m, n);
      int acode;
      int tpos;

      if (board[pos] == EMPTY || !is_worm_origin(pos, pos))
	continue;

      TRACE ("considering attack of %m\n", m, n);
      /* Initialize all relevant fields at once. */
      worm[pos].attack_code   = 0;
      worm[pos].attack_point  = 0;
      worm[pos].defend_code   = 0;
      worm[pos].defense_point = 0;
      acode = attack(pos, &tpos);
      if (acode) {
	TRACE("worm at %m can be attacked at %1m\n", m, n, tpos);
	worm[pos].attack_code = acode;
	worm[pos].attack_point = tpos;
	add_attack_move(tpos, pos);
      }
      propagate_worm(pos);
    }
  gg_assert(stackp == 0);
  
  /* 2. Use pattern matching to find a few more attacks. */
  find_attack_patterns();
  gg_assert(stackp == 0);
  
  /* 3. Now find defense moves. */
  for (m = 0; m < board_size; m++)
    for (n = 0; n < board_size; n++) {
      int pos = POS(m, n);
      int dcode;

      if (board[pos] == EMPTY || !is_worm_origin(pos, pos))
	continue;

      if (worm[pos].attack_code != 0) {
	int tpos = NO_MOVE;

	TRACE ("considering defense of %m\n", m, n);
	dcode = find_defense(pos, &tpos);
	if (dcode) {
	  TRACE ("worm at %m can be defended at %1m\n", m, n, tpos);
	  worm[pos].defend_code   = dcode;
	  worm[pos].defense_point = tpos;
	  if (tpos != NO_MOVE)
	    add_defense_move(tpos, pos);
	}
	else {
	  /* If the point of attack is not adjacent to the worm, 
	   * it is possible that this is an overlooked point of
	   * defense, so we try and see if it defends.
	   */
	  int aa = worm[pos].attack_point;
	  if (!liberty_of_string(aa, pos))
	    if (trymove(aa, worm[pos].color, "make_worms", NO_MOVE,
			EMPTY, NO_MOVE)) {
	      int acode = attack(pos, NULL);
	      if (acode != WIN) {
		int change_defense = 0;
		/* FIXME: Include defense code when move 
		 *        is registered. */
		add_defense_move(aa, pos);

		if (acode == 0) {
		  worm[pos].defend_code = WIN;
		  change_defense = 1;
		}
		else if (acode == KO_B && worm[pos].defend_code != WIN) {
		  worm[pos].defend_code = KO_A;
		  change_defense = 1;
		}
		else if (acode == KO_A && worm[pos].defend_code == 0) {
		  worm[pos].defend_code = KO_B;
		  change_defense = 1;
		}
		
		if (change_defense) {
		  worm[pos].defense_point = aa;
		  TRACE ("worm at %1m can be defended at %1m with code %d\n",
			 pos, aa, worm[pos].defend_code);
		}
	      }	 
	      popgo();
	    }
	}
      }
      propagate_worm(pos);
    }
  gg_assert(stackp == 0);

  /* 4. Use pattern matching to find a few more defense moves. */
  find_defense_patterns();
  gg_assert(stackp == 0);
  
  /*
   * In the new move generation scheme, we need to find all moves that
   * attacks and or defends some string.
   */

  /*
   * 5. Find additional attacks and defenses by testing all immediate
   *    liberties. Further attacks and defenses are found by pattern
   *    matching and by trying whether each attack or defense point
   *    attacks or defends other strings.
   */
  {
    static int libs[MAXLIBS];
    int k;
    int color;
    int other;
    int liberties;

    for (m = 0; m < board_size; m++)
      for (n = 0; n < board_size; n++) {
	int pos = POS(m, n);
	color = board[pos];
	if (color == EMPTY || !is_worm_origin(pos, pos))
	  continue;

	other = OTHER_COLOR(color);
	
	if (worm[pos].attack_code == 0)
	  continue;
	
	/* There is at least one attack on this group. Try the
	 * liberties.
	 */
	liberties = findlib(pos, MAXLIBS, libs);
	
	for (k = 0; k < liberties; k++) {
	  int aa = libs[k];
	  /* Try to attack on the liberty. */
	  if (trymove(aa, other, "make_worms", NO_MOVE,
		       EMPTY, NO_MOVE)) {
	    if (!board[pos] || attack(pos, NULL)) {
	      int dcode;
	      if (!board[pos])
		dcode = 0;
	      else
		dcode = find_defense(pos, NULL);

	      if (dcode == 0
		  || (dcode == KO_B && (worm[pos].attack_code == 0
					|| worm[pos].attack_code == KO_B))
		  || (dcode == KO_A && worm[pos].attack_code == 0))
		add_attack_move(aa, pos);
	    }
	    popgo();
	  }
	  /* Try to defend at the liberty. */
	  if (worm[pos].defend_code != 0)
	    if (trymove(aa, color, "make_worms", NO_MOVE, EMPTY, NO_MOVE)) {
	      int acode = attack(pos, NULL);
	      if (acode == 0
		  || (acode == KO_B && (worm[pos].defend_code == 0
					|| worm[pos].defend_code == KO_B))
		  || (acode == KO_A && worm[pos].defend_code == 0))
		add_defense_move(aa, pos);
	      popgo();
	    }
	}
      }
  }
  gg_assert(stackp == 0);
}


/*
 * Find moves threatening to attack or save all worms.
 */

static void
find_worm_threats()
{
  int m, n;

  for (m = 0; m < board_size; m++)
    for (n = 0; n < board_size; n++) {
      int pos = POS(m, n);
      int liberties;
      static int libs[MAXLIBS];

      int k;
      int l;
      int color;
      int other;

      /* Only work with origins. */
      color = board[pos];
      if (color == EMPTY || !is_worm_origin(pos, pos))
	continue;

      other = OTHER_COLOR(color);

      /* 1. Start with finding attack threats. */
      /* Only try those worms that have no attack. */
      if (worm[pos].attack_code == 0) {

	liberties = findlib(pos, MAXLIBS, libs);

	/* This test would seem to be unnecessary since we only attack
	 * strings with attack_code == 0, but it turns out that single
	 * stones with one liberty that can be captured, but come to
	 * live again in a snap-back get attack_code == 0. 
	 *
	 * The test against 6 liberties is just an optimization.
	 */
	if (liberties > 1 && liberties < 6) {
	  for (k = 0; k < liberties; k++) {
	    int aa = libs[k];

	    /* Try to threaten on the liberty. */
	    if (trymove(aa, other, "threaten attack", pos, EMPTY, NO_MOVE)) {
	      /* FIXME: Support ko also. */
	      if (attack(pos, NULL) == WIN)
		add_attack_threat_move(aa, POS(m, n));
	      popgo();
	    }

	    /* Try to threaten on second order liberties. */
	    for (l = 0; l < 4; l++) {
	      int bb = libs[k] + delta[l];

	      if (!ON_BOARD(bb)
		  || board[bb] != EMPTY
		  || liberty_of_string(bb, pos))
		continue;

	      if (trymove(bb, other, "threaten attack", pos, EMPTY, NO_MOVE)) {
		/* FIXME: Support ko also. */
		if (attack(pos, NULL) == WIN)
		  add_attack_threat_move(bb, POS(m, n));
		popgo();
	      }
	    }
	  }
	}

	/* FIXME: Try other moves also (patterns?). */
      }

      /* 2. Continue with finding defense threats. */
      /* Only try those worms that have an attack. */
      /* FIXME: Support ko also */
      if (worm[pos].attack_code == WIN
	  && worm[pos].defend_code == 0) {

	liberties = findlib(pos, MAXLIBS, libs);

	for (k = 0; k < liberties; k++) {
	  int aa = libs[k];

	  /* Try to threaten on the liberty. */
	  if (trymove(aa, color, "threaten defense", NO_MOVE,
		      EMPTY, NO_MOVE)) {
	    /* FIXME: Support ko also. */
	    if (attack(pos, NULL) == WIN
		&& find_defense(pos, NULL) == WIN)
	      add_defense_threat_move(aa, POS(m, n));
	    popgo();
	  }

	  /* Try to threaten on second order liberties. */
	  for (l = 0; l < 4; l++) {
	    int bb = libs[k] + delta[l];

	    if (!ON_BOARD(bb)
		|| board[bb] != EMPTY
		|| liberty_of_string(bb, pos))
	      continue;

	    if (trymove(bb, other, "threaten defense", pos, EMPTY, NO_MOVE)) {
	      /* FIXME: Support ko also. */
	      if (attack(pos, NULL) == WIN
		  && find_defense(pos, NULL) == WIN)
		add_defense_threat_move(bb, POS(m, n));
	      popgo();
	    }
	  }
	}

	/* FIXME: Try other moves also (patterns?). */
      }
    }
}


/* find_lunch(str, &worm) looks for a worm adjoining the
 * string at (str) which can be easily captured. Whether or not it can
 * be defended doesn't matter.
 *
 * Returns the location of the string in (*lunch).
 */
	
static int
find_lunch(int str, int *lunch)
{
  int i, j;
  int k;

  ASSERT1(board[str] != EMPTY, str);
  ASSERT1(stackp == 0, str);

  *lunch = NO_MOVE;
  for (i = 0; i < board_size; i++)
    for (j = 0; j < board_size; j++) {
      int pos = POS(i, j);
      if (board[pos] != OTHER_COLOR(board[str]))
	continue;
      for (k = 0; k < 8; k++) {
	int apos = pos + delta[k];
	if (ON_BOARD(apos) && is_same_worm(apos, str)) {
	  if (worm[pos].attack_code != 0 && !is_ko_point(pos)) {
	    /*
	     * If several adjacent lunches are found, we pick the 
	     * juiciest. First maximize cutstone, then minimize liberties. 
	     * We can only do this if the worm data is available, 
	     * i.e. if stackp==0.
	     */
	    if (*lunch == NO_MOVE
		|| worm[pos].cutstone > worm[*lunch].cutstone 
		|| (worm[pos].cutstone == worm[*lunch].cutstone 
		    && worm[pos].liberties < worm[*lunch].liberties)) {
	      *lunch = worm[pos].origin;
	    }
	  }
	  break;
	}
      }
    }

  if (*lunch != NO_MOVE)
    return 1;

  return 0;
}


/*
 * Test whether two worms are the same. Used by autohelpers.
 * Before this function can be called, build_worms must have been run.
 */

int
is_same_worm(int w1, int w2)
{
  return worm[w1].origin == worm[w2].origin;
}


/*
 * Test whether the origin of the worm at (wi, wj) is (i, j).
 */

int
is_worm_origin(int w, int pos)
{
  return worm[w].origin == pos;
}


/* 
 * propagate_worm() takes the worm data at one stone and copies it to 
 * the remaining members of the worm.
 *
 * Even though we don't need to copy all the fields, it's probably
 * better to do a structure copy which should compile to a block copy.
 */

void 
propagate_worm(int pos)
{
  int k;
  int num_stones;
  int stones[MAX_BOARD * MAX_BOARD];
  gg_assert(stackp == 0);
  ASSERT1(board[pos] != EMPTY, pos);

  num_stones = findstones(pos, MAX_BOARD * MAX_BOARD, stones);
  for (k = 0; k < num_stones; k++)
    if (stones[k] != pos)
      worm[stones[k]] = worm[pos];
}


/* ping_cave(i, j, *lib1, ...) is applied when (i, j) points to a string.
 * It computes the vector (*lib1, *lib2, *lib3, *lib4), 
 * where *lib1 is the number of liberties of the string, 
 * *lib2 is the number of second order liberties (empty vertices
 * at distance two) and so forth.
 *
 * The definition of liberties of order >1 is adapted to the problem
 * of detecting the shape of the surrounding cavity. In particular
 * we want to be able to see if a group is loosely surrounded.
 *
 * A liberty of order n is an empty space which may be connected
 * to the string by placing n stones of the same color on the board, 
 * but no fewer. The path of connection may pass through an intervening group
 * of the same color. The stones placed at distance >1 may not touch a
 * group of the opposite color. At the edge, also diagonal neighbors
 * count as touching. The path may also not pass through a liberty at distance
 * 1 if that liberty is flanked by two stones of the opposing color. This
 * reflects the fact that the O stone is blocked from expansion to the
 * left by the two X stones in the following situation:
 * 
 *          X.
 *          .O
 *          X.
 *
 * On the edge, one stone is sufficient to block expansion:
 *
 *          X.
 *          .O
 *          --
 */

static void 
ping_cave(int str, int *lib1, int *lib2, int *lib3, int *lib4)
{
  int pos;
  int k;
  int libs[MAXLIBS];
  int mrc[BOARDMAX];
  int mse[BOARDMAX];
  int color = board[str];
  int other = OTHER_COLOR(color);

  memset(mse, 0, sizeof(mse));

  /* Find and mark the first order liberties. */
  *lib1 = findlib(str, MAXLIBS, libs);
  for (k = 0; k < *lib1; k++)
    mse[libs[k]] = 1;

  /* Reset mse at liberties which are flanked by two stones of the
   * opposite color, or one stone and the edge.
   */

  for (pos = BOARDMIN; pos < BOARDMAX; pos++)
    if (ON_BOARD(pos)
	&& mse[pos]
	&& (((      !ON_BOARD(SOUTH(pos)) || board[SOUTH(pos)] == other)
	     && (   !ON_BOARD(NORTH(pos)) || board[NORTH(pos)] == other))
	    || ((   !ON_BOARD(WEST(pos))  || board[WEST(pos)] == other)
		&& (!ON_BOARD(EAST(pos))  || board[EAST(pos)] == other))))
      mse[pos] = 0;
  
  *lib2 = 0;
  memset(mrc, 0, sizeof(mrc));
  ping_recurse(str, lib2, mse, mrc, color);

  *lib3 = 0;
  memset(mrc, 0, sizeof(mrc));
  ping_recurse(str, lib3, mse, mrc, color);

  *lib4 = 0;
  memset(mrc, 0, sizeof(mrc));
  ping_recurse(str, lib4, mse, mrc, color);
}


/* recursive function called by ping_cave */

static void 
ping_recurse(int pos, int *counter,
	     int mx[BOARDMAX], int mr[BOARDMAX],
	     int color)
{
  int k;
  mr[pos] = 1;

  for (k = 0; k < 4; k++) {
    int apos = pos + delta[k];
    if (board[apos] == EMPTY
	&& mx[apos] == 0
	&& mr[apos] == 0
	&& !touching(apos, OTHER_COLOR(color))) {
      (*counter)++;
      mr[apos] = 1;
      mx[apos] = 1;
    }
  }
  
  if (!is_ko_point(pos)) {
    for (k = 0; k < 4; k++) {
      int apos = pos + delta[k];
      if (ON_BOARD(apos)
	  && mr[apos] == 0
	  && (mx[apos] == 1
	      || board[apos] == color))
	ping_recurse(apos, counter, mx, mr, color);
    }
  }
}


/* touching(i, j, color) returns true if the vertex at (i, j) is
 * touching any stone of (color).
 */

static int
touching(int pos, int color)
{
  return (board[SOUTH(pos)] == color
	  || board[WEST(pos)] == color
	  || board[NORTH(pos)] == color
	  || board[EAST(pos)] == color);
}


/* The GENUS of a string is the number of connected components of
 * its complement, minus one. It is an approximation to the number of
 * eyes of the string. If (i, j) points to the origin of a string,
 * genus(i, j) returns its genus.
 */

static int 
genus(int str)
{
  int m, n;
  int mg[BOARDMAX];
  int gen = -1;

  memset(mg, 0, sizeof(mg));
  for (m = 0; m < board_size; m++)
    for (n = 0; n < board_size; n++) {
      int pos = POS(m, n);
      if (!mg[pos] && (board[pos] == EMPTY || !is_worm_origin(pos, str))) {
	markcomponent(str, pos, mg);
	gen++;
      }
    }

  return gen;
}


/* This recursive function marks the component at (m, n) of 
 * the complement of the string with origin (i, j)
 */

static void 
markcomponent(int str, int pos, int mg[BOARDMAX])
{
  int k;
  mg[pos] = 1;
  for (k = 0; k < 4; k++) {
    int apos = pos + delta[k];
    if (ON_BOARD(apos)
	&& mg[apos] == 0
	&& (board[apos] == EMPTY || !is_worm_origin(apos, str)))
      markcomponent(str, apos, mg);
  }
}


/* examine_cavity(m, n, *edge), if (m, n) is EMPTY, examines the
 * cavity at (m, n) and returns its bordercolor,
 * which can be BLACK_BORDER, WHITE_BORDER or GRAY_BORDER. The edge
 * parameter is set to the number of edge vertices in the cavity.
 *
 * If (m, n) is nonempty, it returns the same result, imagining
 * that the string at (m, n) is removed. The edge parameter is
 * set to the number of vertices where the cavity meets the
 * edge in a point outside the removed string.  
 */

static int
examine_cavity(int pos, int *edge)
{
  int border_color = EMPTY;
  int ml[BOARDMAX];
  int origin = NO_MOVE;
  
  ASSERT_ON_BOARD1(pos);
  gg_assert(edge != NULL);
  
  memset(ml, 0, sizeof(ml));

  *edge = 0;

  if (board[pos] != EMPTY)
    origin = find_origin(pos);
  
  cavity_recurse(pos, ml, &border_color, edge, origin);

  if (border_color == (BLACK | WHITE))
    return GRAY_BORDER;  
  if (border_color == BLACK)
    return BLACK_BORDER;
  if (border_color == WHITE)
    return WHITE_BORDER;

  /* We should have returned now, unless the board is completely empty.
   * Verify that this is the case and then return GRAY_BORDER.
   */
  gg_assert(border_color == EMPTY && stones_on_board(BLACK | WHITE) == 0);
  
  return GRAY_BORDER;
}


/* helper function for examine_cavity.
 * border_color contains information so far : transitions allowed are
 *   EMPTY       -> BLACK/WHITE
 *   BLACK/WHITE -> BLACK | WHITE
 *
 * mx[i][j] is 1 if (i, j) has already been visited.
 *
 * if (ai, aj) points to the origin of a string, it will be ignored.
 *
 * On (fully-unwound) exit
 *   *border_color should be BLACK, WHITE or BLACK | WHITE
 *   *edge is the count of edge pieces
 *
 * *border_color should be EMPTY if and only if the board
 * is completely empty or only contains the ignored string.
 */

static void 
cavity_recurse(int pos, int mx[BOARDMAX], 
	       int *border_color, int *edge, int str)
{
  int k;
  ASSERT1(mx[pos] == 0, pos);

  mx[pos] = 1;

  
  if ((!ON_BOARD(SOUTH(pos)) || !ON_BOARD(WEST(pos))
       || !ON_BOARD(NORTH(pos)) || !ON_BOARD(EAST(pos)))
      && board[pos] == EMPTY) 
    (*edge)++;

  /* Loop over the four neighbors. */
  for (k = 0; k < 4; k++) {
    int apos = pos + delta[k];
    if (ON_BOARD(apos) && !mx[apos]) {
      int neighbor_empty = 0;
      
      if (board[apos] == EMPTY)
	neighbor_empty = 1;
      else {
	/* Count the neighbor as empty if it is part of the (ai, aj) string. */
	if (str == find_origin(apos))
	  neighbor_empty = 1;
	else
	  neighbor_empty = 0;
      }
      
      if (!neighbor_empty)
	*border_color |= board[apos];
      else
	cavity_recurse(apos, mx, border_color, edge, str);
    }
  }
}


/* Find attacking moves by pattern matching, for both colors. */
static void
find_attack_patterns(void)
{
  global_matchpat(attack_callback, ANCHOR_OTHER, &attpat_db, NULL, NULL);
}

/* Try to attack every X string in the pattern, whether there is an attack
 * before or not. Only exclude already known attacking moves.
 */
static void
attack_callback(int m, int n, int color, struct pattern *pattern, int ll,
		void *data)
{
  int ti, tj;
  int move;
  int k;
  UNUSED(data);

  TRANSFORM(pattern->movei, pattern->movej, &ti, &tj, ll);
  ti += m;
  tj += n;
  move = POS(ti, tj);

  /* If the pattern has a constraint, call the autohelper to see
   * if the pattern must be rejected.
   */
  if (pattern->autohelper_flag & HAVE_CONSTRAINT) {
    if (!pattern->autohelper(pattern, ll, ti, tj, color, 0))
      return;
  }

  /* If the pattern has a helper, call it to see if the pattern must
   * be rejected.
   */
  if (pattern->helper) {
    if (!pattern->helper(pattern, ll, ti, tj, color)) {
      TRACE("Attack pattern %s+%d rejected by helper at %1m\n", pattern->name,
	    ll, move);
      return;
    }
  }

  /* Loop through pattern elements in search of X strings to attack. */
  for (k = 0; k < pattern->patlen; ++k) { /* match each point */
    if (pattern->patn[k].att == ATT_X) {
      /* transform pattern real coordinate */
      int x, y;
      int aa;
      TRANSFORM(pattern->patn[k].x,pattern->patn[k].y,&x,&y,ll);
      x += m;
      y += n;

      aa = worm[POS(x, y)].origin;

      /* A string with 5 liberties or more is considered tactically alive. */
      if (countlib(aa) > 4)
	continue;

      if (attack_move_known(move, aa))
	continue;

      /* No defenses are known at this time, so defend_code is always 0. */
#if 0
      /* If the string can be attacked but not defended, ignore it. */
      if (worm[aa].attack_code == WIN && worm[aa].defend_code == 0)
	continue;
#endif
      
      /* FIXME: Don't attack the same string more than once.
       * Play (ti, tj) and see if there is a defense.
       */
      if (trymove(move, color, "attack_callback", aa, EMPTY, NO_MOVE)) {
	int dcode;
	if (!board[aa])
	  dcode = 0;
	else if (!attack(aa, NULL))
	  dcode = WIN;
	else
	  dcode = find_defense(aa, NULL);

	popgo();

	if (dcode == 0) {
	  TRACE("Attack pattern %s+%d found attack on %1m at %1m\n",
		pattern->name, ll, aa, move);
	  worm[aa].attack_code = WIN;
	  worm[aa].attack_point = move;
	}
	else if (dcode == KO_A) {
	  TRACE("Attack pattern %s+%d found attack on %1m at %m1 with ko (acode=%d)\n",
		pattern->name, ll, aa, move, KO_A);
	  if (worm[aa].attack_code == KO_B 
	      || worm[aa].attack_code == 0) {
	    worm[aa].attack_code = KO_B;
	    worm[aa].attack_point = move;
	  }
	}
	else if (dcode == KO_B) {
	  TRACE("Attack pattern %s+%d found attack on %1m at %1m with ko (acode=%d)\n",
		pattern->name, ll, aa, move, KO_B);
	  if (worm[aa].attack_code != WIN) {
	    worm[aa].attack_code = KO_A;
	    worm[aa].attack_point = move;
	  }
	}

	if (dcode != WIN) {
	  propagate_worm(aa);
	  add_attack_move(move, aa);
	}
      }
    }
  }
}

static void
find_defense_patterns(void)
{
  global_matchpat(defense_callback, ANCHOR_COLOR, &defpat_db, NULL, NULL);
}

static void
defense_callback(int m, int n, int color, struct pattern *pattern, int ll,
		 void *data)
{
  int ti, tj;
  int move;
  int k;
  UNUSED(data);

  TRANSFORM(pattern->movei, pattern->movej, &ti, &tj, ll);
  ti += m;
  tj += n;
  move = POS(ti, tj);
  
  /* If the pattern has a constraint, call the autohelper to see
   * if the pattern must be rejected.
   */
  if (pattern->autohelper_flag & HAVE_CONSTRAINT) {
    if (!pattern->autohelper(pattern, ll, ti, tj, color, 0))
      return;
  }

  /* If the pattern has a helper, call it to see if the pattern must
   * be rejected.
   */
  if (pattern->helper) {
    if (!pattern->helper(pattern, ll, ti, tj, color)) {
      TRACE("Defense pattern %s+%d rejected by helper at %1m\n", pattern->name,
	    ll, move);
      return;
    }
  }

  /* Loop through pattern elements in search for O strings to defend. */
  for (k = 0; k < pattern->patlen; ++k) { /* match each point */
    if (pattern->patn[k].att == ATT_O) {
      /* transform pattern real coordinate */
      int x, y;
      int aa;
      TRANSFORM(pattern->patn[k].x, pattern->patn[k].y, &x, &y, ll);
      x += m;
      y += n;

      aa = worm[POS(x, y)].origin;

      if (worm[aa].attack_code == 0
	  || defense_move_known(move, aa))
	continue;
      
      /* FIXME: Don't try to defend the same string more than once.
       * FIXME: For all attacks on this string, we should test whether
       *        the proposed move happens to refute the attack.
       * Play (ti, tj) and see if there is an attack. */
      if (trymove(move, color, "defense_callback", aa, EMPTY, NO_MOVE)) {
	int acode = attack(aa, NULL);

	popgo();
	
	if (acode == 0) {
	  TRACE("Defense pattern %s+%d found defense of %1m at %1m\n",
		pattern->name, ll, aa, move);
	  worm[aa].defend_code   = WIN;
	  worm[aa].defense_point = move;
	}
	else if (acode == KO_A) {
	  TRACE("Defense pattern %s+%d found defense of %1m at %1m with ko (acode=%d)\n",
		pattern->name, ll, aa, move, 3);
	  if (worm[aa].defend_code != WIN) {
	    worm[aa].defend_code   = KO_B;
	    worm[aa].defense_point = move;
	  }
	}
	else if (acode == KO_B) {
	  TRACE("Defense pattern %s+%d found defense of %1m at %1m with ko (acode=%d)\n",
		pattern->name, ll, aa, move, 3);
	  if (worm[aa].defend_code != WIN) {
	    worm[aa].defend_code   = KO_A;
	    worm[aa].defense_point = move;
	  }
	}

	if (acode != WIN) {
	  propagate_worm(aa);
	  add_defense_move(move, aa);
	}
      }
    }
  }
}

/* ================================================================ */
/*                      Debugger functions                          */
/* ================================================================ */

/* For use in gdb, print details of the worm at (m,n). 
 * Add this to your .gdbinit file:
 *
 * define worm
 * set ascii_report_worm("$arg0")
 * end
 *
 * Now 'worm S8' will report the details of the S8 worm.
 *
 */

void
ascii_report_worm(char *string)
{
  int m, n;
  string_to_location(board_size, string, &m, &n);
  report_worm(m, n);
}


void
report_worm(int m, int n)
{
  int pos = POS(m, n);
  if (board[pos] == EMPTY) {
    gprintf("There is no worm at %1m\n", pos);
    return;
  }

  gprintf("*** worm at %1m:\n", pos);
  gprintf("color: %s; origin: %1m; size: %d; effective size: %f\n",
	  (worm[pos].color == WHITE) ? "White" : "Black",
	  worm[pos].origin, worm[pos].size, worm[pos].effective_size);

  gprintf("liberties: %d order 2 liberties:%d order 3:%d order 4:%d\n",
	  worm[pos].liberties, 
	  worm[pos].liberties2, 
	  worm[pos].liberties3, 
	  worm[pos].liberties4);

  if (worm[pos].attack_point != NO_MOVE)
    gprintf("attack point %1m, ", worm[pos].attack_point);
  else
    gprintf("no attack point, ");

  if (worm[pos].attack_code == 1)
    gprintf("attack code WIN\n");
  else if (worm[pos].attack_code == KO_A)
    gprintf("attack code KO_A\n");
  else if (worm[pos].attack_code == KO_B)
    gprintf("attack code KO_B\n");

  if (worm[pos].defense_point != NO_MOVE)
    gprintf("defense point %1m, ", worm[pos].defense_point);
  else
    gprintf("no defense point, ");

  if (worm[pos].defend_code == 1)
    gprintf("defend code WIN\n");
  else if (worm[pos].defend_code == KO_A)
    gprintf("defend code KO_A\n");
  else if (worm[pos].defend_code == KO_B)
    gprintf("defend code KO_B\n");

  if (worm[pos].lunch != NO_MOVE)
    gprintf("lunch at %1m\n", worm[pos].lunch);

  gprintf("cutstone: %d, cutstone2: %d\n",
	  worm[pos].cutstone, worm[pos].cutstone2);

  gprintf("genus: %d, ", worm[pos].genus);

  if (worm[pos].inessential)
    gprintf("inessential: YES, ");
  else
    gprintf("inessential: NO, ");

  if (worm[pos].invincible)
    gprintf("invincible: YES, \n");
  else
    gprintf("invincible: NO, \n");

  if (worm[pos].unconditional_status == ALIVE)
    gprintf("unconditional status ALIVE\n");
  else if (worm[pos].unconditional_status == DEAD)
    gprintf("unconditional status DEAD\n");
  else if (worm[pos].unconditional_status == WHITE_BORDER)
    gprintf("unconditional status WHITE_BORDER\n");
  else if (worm[pos].unconditional_status == BLACK_BORDER)
    gprintf("unconditional status BLACK_BORDER\n");
  else if (worm[pos].unconditional_status == UNKNOWN)
    gprintf("unconditional status UNKNOWN\n");
}


/*
 * Local Variables:
 * tab-width: 8
 * c-basic-offset: 2
 * End:
 */
