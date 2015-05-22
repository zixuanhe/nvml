/*
 * Copyright (c) 2015, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * pminvaders.c -- example usage of non-tx allocations
 */

#include <stddef.h>
#include <ncurses.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libpmem.h>
#include <libpmemobj.h>

#define	LAYOUT_NAME "pminvaders"

#define	PMINVADERS_POOL_SIZE	(100 * 1024 * 1024) /* 100 megabytes */

#define	GAME_WIDTH	30
#define	GAME_HEIGHT	30

#define	RRAND(min, max)	(rand() % ((max) - (min) + 1) + (min))

#define	STEP	50

#define	PLAYER_Y (GAME_HEIGHT - 1)
#define	MAX_GSTATE_TIMER 10000
#define	MIN_GSTATE_TIMER 5000

#define	MAX_ALIEN_TIMER	1000

#define	MAX_PLAYER_TIMER 1000
#define	MAX_BULLET_TIMER 500

enum alloc_type {
	ALLOC_UNKNOWN,
	ALLOC_GAME_STATE,
	ALLOC_PLAYER,
	ALLOC_ALIEN,
	ALLOC_BULLET,

	MAX_ALLOC
};

enum colors {
	C_UNKNOWN,
	C_PLAYER,
	C_ALIEN,
	C_BULLET,

	MAX_C
};

struct game_state {
	uint32_t timer; /* alien spawn timer */
	uint16_t score;
	uint16_t high_score;
};

struct alien {
	uint16_t x;
	uint16_t y;
	uint32_t timer; /* movement timer */
};

struct player {
	uint16_t x;
	uint32_t timer; /* weapon cooldown */
	uint16_t padding; /* to 8 bytes */
};

struct bullet {
	uint16_t x;
	uint16_t y;
	uint32_t timer; /* movement timer */
};

static PMEMobjpool *pop;
static struct game_state *gstate;

/*
 * create_alien -- constructor for aliens, spawn at random position
 */
void
create_alien(void *ptr, void *arg)
{
	struct alien *a = ptr;
	a->y = 1;
	a->x = RRAND(2, GAME_WIDTH - 2);
	a->timer = 1;

	pmem_persist(a, sizeof (*a));
}

/*
 * create_player -- constructor for the player, spawn in the middle of the map
 */
void
create_player(void *ptr, void *arg)
{
	struct player *p = ptr;
	p->x = GAME_WIDTH / 2;
	p->timer = 1;

	pmem_persist(p, sizeof (*p));
}

/*
 * create_bullet -- constructor for bullets, spawn at the position of the player
 */
void
create_bullet(void *ptr, void *arg)
{
	struct bullet *b = ptr;
	struct player *p = arg;

	b->x = p->x;
	b->y = PLAYER_Y - 1;
	b->timer = 1;

	pmem_persist(b, sizeof (*b));
}

void
draw_border()
{
	for (int x = 0; x <= GAME_WIDTH; ++x) {
		mvaddch(0, x, ACS_HLINE);
		mvaddch(GAME_HEIGHT, x, ACS_HLINE);
	}
	for (int y = 0; y <= GAME_HEIGHT; ++y) {
		mvaddch(y, 0, ACS_VLINE);
		mvaddch(y, GAME_WIDTH, ACS_VLINE);
	}
	mvaddch(0, 0, ACS_ULCORNER);
	mvaddch(GAME_HEIGHT, 0, ACS_LLCORNER);
	mvaddch(0, GAME_WIDTH, ACS_URCORNER);
	mvaddch(GAME_HEIGHT, GAME_WIDTH, ACS_LRCORNER);
}

void
draw_alien(const struct alien *a)
{
	mvaddch(a->y, a->x, ACS_DIAMOND|COLOR_PAIR(C_ALIEN));
}

void
draw_player(const struct player *p)
{
	mvaddch(PLAYER_Y, p->x, ACS_DIAMOND|COLOR_PAIR(C_PLAYER));
}

void
draw_bullet(const struct bullet *b)
{
	mvaddch(b->y, b->x, ACS_BULLET|COLOR_PAIR(C_BULLET));
}

void
draw_score()
{
	mvprintw(1, 1, "Score: %u | %u\n", gstate->score, gstate->high_score);
}

/*
 * timer_tick -- very simple persistent timer
 */
int
timer_tick(uint32_t *timer)
{
	int ret = *timer == 0 || ((*timer)--) == 0;
	pmem_persist(timer, sizeof (*timer));
	return ret;
}

/*
 * update_score -- change player score and global high score
 */
void
update_score(int m)
{
	if (m < 0 && gstate->score == 0)
		return;

	uint16_t score = gstate->score + m;
	uint16_t highscore = score > gstate->high_score ?
		score : gstate->high_score;
	struct game_state s = {
		.timer = gstate->timer,
		.score = score,
		.high_score = highscore
	};

	*gstate = s;
	pmem_persist(gstate, sizeof (*gstate));
}

/*
 * process_aliens -- process spawn and movement of the aliens
 */
void
process_aliens()
{
	/* alien spawn timer */
	if (timer_tick(&gstate->timer)) {
		gstate->timer = RRAND(MIN_GSTATE_TIMER, MAX_GSTATE_TIMER);
		pmem_persist(gstate, sizeof (*gstate));
		pmemobj_alloc_construct(pop, sizeof (struct alien), ALLOC_ALIEN,
			create_alien, NULL);
	}

	OID_TYPE(struct alien) iter, next;
	POBJ_FOREACH_SAFE_TYPE(pop, iter, next, ALLOC_ALIEN) {
		if (timer_tick(&D_RW(iter)->timer)) {
			D_RW(iter)->timer = MAX_ALIEN_TIMER;
			D_RW(iter)->y++;
		}
		pmem_persist(D_RW(iter), sizeof (struct alien));
		draw_alien(D_RO(iter));

		/* decrease the score if the ship wasn't intercepted */
		if (D_RO(iter)->y > GAME_HEIGHT - 1) {
			pmemobj_free(iter.oid);
			update_score(-1);
			pmem_persist(gstate, sizeof (*gstate));
		}
	}
}

/*
 * process_collision -- search for any aliens on the position of the bullet
 */
int
process_collision(const struct bullet *b)
{
	OID_TYPE(struct alien) iter;

	POBJ_FOREACH_TYPE(pop, iter, ALLOC_ALIEN) {
		if (b->x == D_RO(iter)->x && b->y == D_RO(iter)->y) {
			update_score(1);
			pmemobj_free(iter.oid);
			return 1;
		}
	}

	return 0;
}

/*
 * process_bullets -- process bullets movement and collision
 */
void
process_bullets()
{
	OID_TYPE(struct bullet) iter, next;

	POBJ_FOREACH_SAFE_TYPE(pop, iter, next, ALLOC_BULLET) {
		/* bullet movement timer */
		if (timer_tick(&D_RW(iter)->timer)) {
			D_RW(iter)->timer = MAX_BULLET_TIMER;
			D_RW(iter)->y--;
		}
		pmem_persist(D_RW(iter), sizeof (struct bullet));

		draw_bullet(D_RO(iter));
		if (D_RO(iter)->y == 0 || process_collision(D_RO(iter)))
			pmemobj_free(iter.oid);
	}
}

/*
 * process_player -- handle player actions
 */
void
process_player(int input)
{
	OID_TYPE(struct player) plr;
	OID_ASSIGN(plr, pmemobj_first(pop, ALLOC_PLAYER));

	/* weapon cooldown tick */
	timer_tick(&D_RW(plr)->timer);

	if (input == KEY_LEFT) {
		uint16_t dstx = D_RO(plr)->x - 1;
		if (dstx != 0)
			D_RW(plr)->x = dstx;
	} else if (input == KEY_RIGHT) {
		uint16_t dstx = D_RO(plr)->x + 1;
		if (dstx != GAME_WIDTH - 1)
			D_RW(plr)->x = dstx;
	} else if (input == ' ' && D_RO(plr)->timer == 0) {
		D_RW(plr)->timer = MAX_PLAYER_TIMER;
		pmemobj_alloc_construct(pop, sizeof (struct bullet),
			ALLOC_BULLET, create_bullet, D_RW(plr));
	}

	pmem_persist(D_RW(plr), sizeof (struct player));

	draw_player(D_RO(plr));
}

/*
 * game_loop -- process drawing and logic of the game
 */
void
game_loop(int input)
{
	erase();
	draw_score();
	draw_border();
	process_aliens();
	process_bullets();
	process_player(input);
	usleep(STEP);
	refresh();
}

int
main(int argc, char *argv[])
{
	if (argc != 2) {
		printf("usage: %s file-name\n", argv[0]);
		return 1;
	}

	const char *path = argv[1];

	pop = NULL;

	srand(time(NULL));

	if (access(path, F_OK) != 0) {
		if ((pop = pmemobj_create(path, LAYOUT_NAME,
			PMINVADERS_POOL_SIZE, S_IRWXU)) == NULL) {
			printf("failed to create pool\n");
			return 1;
		}

		/* create the player and initialize with a constructor */
		pmemobj_alloc_construct(pop, sizeof (struct player),
			ALLOC_PLAYER, create_player, NULL);

	} else {
		if ((pop = pmemobj_open(path, LAYOUT_NAME)) == NULL) {
			printf("failed to open pool\n");
			return 1;
		}
	}

	/* global state of the game is kept in the root object */
	gstate = pmemobj_direct(pmemobj_root(pop, sizeof (struct game_state)));

	initscr();
	start_color();
	init_pair(C_PLAYER, COLOR_GREEN, COLOR_BLACK);
	init_pair(C_ALIEN, COLOR_RED, COLOR_BLACK);
	init_pair(C_BULLET, COLOR_YELLOW, COLOR_BLACK);
	nodelay(stdscr, true);
	curs_set(0);
	keypad(stdscr, true);

	int in;
	while ((in = getch()) != 'q') {
		game_loop(in);
	}

	pmemobj_close(pop);

	endwin();

	return 0;
}