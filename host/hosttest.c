// Host test for the game rules. Prints the self-test checksum, which must
// match the one the debug PRC logs on the Palm.
#include <stdio.h>
#include "../src/game/game.h"

int main(void)
{
	uint32_t sum = game_selftest();
	GameState a, b;
	int i;

	// Same seed and inputs give the same state.
	game_init(&a, 777);
	game_init(&b, 777);
	for (i = 0; i < 500; i++) {
		game_tick(&a, (uint16_t)i);
		game_tick(&b, (uint16_t)i);
	}
	if (game_checksum(&a) != game_checksum(&b)) {
		printf("FAIL determinism\n");
		return 1;
	}
	game_tick(&b, 1);
	if (game_checksum(&a) == game_checksum(&b)) {
		printf("FAIL checksum ignores state\n");
		return 1;
	}
	printf("selftest checksum %08lx\n", (unsigned long)sum);
	return 0;
}
