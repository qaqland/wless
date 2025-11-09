#include <SDL3/SDL_init.h>
#include <SDL3/SDL_keycode.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#define SDL_MAIN_USE_CALLBACKS 1

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

struct AppState {
	int max_size;
	int min_size;

	SDL_Window *window;
	SDL_Renderer *renderer;
};

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
	struct AppState *state = calloc(1, sizeof(*state));

	int c = 0;
	while ((c = getopt(argc, argv, "a:b:hv")) != -1) {
		switch (c) {
		case 'a':
			state->max_size = atoi(optarg);
			break;
		case 'b':
			state->min_size = atoi(optarg);
			break;
		case 'h':
			fprintf(stdout, "resize -a MAX -b MIN\n");
			return SDL_APP_SUCCESS;
		case 'v':
			fprintf(stdout, "resize 0.1\n");
			return SDL_APP_SUCCESS;
		case '?':
			fprintf(stdout, "resize -a MAX -b MIN\n");
			return SDL_APP_FAILURE;
		}
	}

	if (state->max_size == 0 || state->min_size == 0) {
		fprintf(stdout, "resize -a MAX -b MIN\n");
		return SDL_APP_FAILURE;
	}

	bool ok = true;

	ok = SDL_SetAppMetadata("example resize", "1.0", "land.qaq.resize");
	if (!ok) {
		return SDL_APP_FAILURE;
	}

	ok = SDL_Init(SDL_INIT_VIDEO);
	if (!ok) {
		SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}

	ok = SDL_CreateWindowAndRenderer("example-resize", state->min_size,
					 state->min_size, 0, &state->window,
					 &state->renderer);
	if (!ok) {
		SDL_Log("Couldn't create window/renderer: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}

	*appstate = state;
	return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
	struct AppState *state = appstate;
	static int size = 0;

	if (event->type == SDL_EVENT_QUIT) {
		return SDL_APP_SUCCESS;
	}
	if (event->type == SDL_EVENT_KEY_DOWN) {
		switch (event->key.key) {
		case SDLK_SPACE:
			size = size == state->max_size ? state->min_size
						       : state->max_size;
			SDL_SetWindowSize(state->window, size, size);
			break;
		case SDLK_ESCAPE:
			return SDL_APP_SUCCESS;
		}
	}
	return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
	struct AppState *state = appstate;

	double now = ((double) SDL_GetTicks()) / 1000.0;
	float red = (float) (0.5 + 0.5 * SDL_sin(now));
	float green = (float) (0.5 + 0.5 * SDL_sin(now + SDL_PI_D * 2 / 3));
	float blue = (float) (0.5 + 0.5 * SDL_sin(now + SDL_PI_D * 4 / 3));
	SDL_SetRenderDrawColorFloat(state->renderer, red, green, blue,
				    SDL_ALPHA_OPAQUE_FLOAT);

	SDL_RenderClear(state->renderer);
	SDL_RenderPresent(state->renderer);

	return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
	free(appstate);
	(void) result;
}
