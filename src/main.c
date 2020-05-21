#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <xcb/xcb.h>
#include <xcb/xcb_cursor.h>

xcb_connection_t* conn = NULL;
xcb_screen_t* screen = NULL;
char running;
char grab_pointer = 0;
char print_window = 0;
enum {
	OUTPUT_PRESS,
	OUTPUT_RELEASE,
	OUTPUT_GEOMETRY,
	OUTPUT_JSON
} output_type = OUTPUT_RELEASE;

struct click_info {
	int press_x;
	int press_y;
	int release_x;
	int release_y;
	int min_x;
	int min_y;
	int width;
	int height;
	unsigned int press_window;
	unsigned int release_window;
};

struct motion_info {
	int x;
	int y;
	unsigned int window;
};

void print_click_json(struct click_info* c)
{
	printf("{\"type\": \"click\", \"x\": %i, \"y\": %i, \"width\": %i, \"height\": %i, \"press\": {\"window\": \"0x%X\", \"x\": %i, \"y\": %i}, \"release\": {\"window\": \"0x%X\", \"x\": %i, \"y\": %i}}\n",
			c->min_x, c->min_y, c->width, c->height,
			c->press_window, c->press_x, c->press_y,
			c->release_window, c->release_x, c->release_y);
}

void print_motion_json(struct motion_info* m)
{
	printf("{\"type\": \"motion\", \"window\": \"0x%X\", \"x\": %i, \"y\": %i}\n",
			m->window, m->x, m->y);
}

void maybe_print_window(unsigned int wid)
{
	if (print_window)
		printf("window 0x%X\n", wid);
}

void print_click_geometry(struct click_info* c)
{
	printf("%ix%i+%i+%i\n",
			c->width, c->height, c->min_x, c->min_y);
	maybe_print_window(c->release_window);
}

void print_click_press(struct click_info* c)
{
	printf("%i %i\n",
			c->press_x, c->press_y);
	maybe_print_window(c->press_window);
}

void print_click_release(struct click_info* c)
{
	printf("%i %i\n",
			c->release_x, c->release_y);
	maybe_print_window(c->release_window);
}

void print_motion(struct motion_info* m)
{
	printf("motion %i %i\n",
			m->x, m->y);
	maybe_print_window(m->window);
}

void sig_handler(int sig)
{
	if (screen != NULL)
	{
		if (grab_pointer)
			xcb_ungrab_pointer(
					conn,
					XCB_CURRENT_TIME
					);
		else
			xcb_ungrab_button(
					conn,
					XCB_BUTTON_INDEX_1,
					screen->root,
					XCB_MOD_MASK_ANY
					);
		xcb_flush(conn);
	}
	running = 0;
	exit(0);
}

int main(int argc, char ** argv)
{
	char show_usage = 0;
	char report_motion = 0;

	int argi;
	for (argi = 1; argi < argc; argi++)
	{
		char* arg = argv[argi];
		if (arg[0] == '-')
		{
			if (arg[1] == '-')
			{
				arg += 2;
				if (!strcmp(arg, "grab"))
				{
					grab_pointer = 1;
				}
				else if (!strcmp(arg, "motion"))
				{
					report_motion = 1;
				}
				else if (!strcmp(arg, "window"))
				{
					print_window = 1;
				}
				else if (!strcmp(arg, "help"))
				{
					show_usage = 1;
				}
				else if (!strcmp(arg, "press"))
				{
					output_type = OUTPUT_PRESS;
				}
				else if (!strcmp(arg, "release"))
				{
					output_type = OUTPUT_RELEASE;
				}
				else if (!strcmp(arg, "geometry"))
				{
					output_type = OUTPUT_GEOMETRY;
				}
				else if (!strcmp(arg, "json"))
				{
					output_type = OUTPUT_JSON;
				}
				else
				{
					fprintf(stderr, "Unknown option: \"%s\"\n", arg - 2);
					return 2;
				}
			}
			else
			{
				while (arg[1] != 0)
				{
					++arg;
					switch(*arg)
					{
						case 'G': grab_pointer = 1; break;
						case 'm': report_motion = 1; break;
						case 'w': print_window = 1; break;
						case 'h': show_usage = 1; break;
						case 'p': output_type = OUTPUT_PRESS; break;
						case 'r': output_type = OUTPUT_RELEASE; break;
						case 'g': output_type = OUTPUT_GEOMETRY; break;
						case 'j': output_type = OUTPUT_JSON; break;
						default:
							fprintf(stderr, "Unknown option: \"-%c\"\n", *arg);
							return 2;
					}
				}
			}
		}
		else
		{
			fprintf(stderr, "Option expected: \"%s\"\n", arg);
			return 2;
		}
	}

	if (show_usage)
	{
		printf("Usage: %s <OPTIONS...>\n"\
				"Basic options:\n"\
				"  -G, --grab              Grab pointer (else grab button)\n"\
				"  -m, --motion            Report motion events\n"\
				"  -w, --window            Print window id (always in json)\n"\
				"  -h, --help              Show this message\n"\
				"Output options (mutually exclusive):\n"\
				"  -p, --press             Print press position\n"\
				"  -r, --release           Print release position (default)\n"\
				"  -g, --geometry          Print geometry of selected rectangle"\
					" (WxH+X+Y)\n"\
				"  -j, --json              Print all data in JSON\n"\
				, argv[0]);
		return 0;
	}

	conn = xcb_connect(NULL, NULL);
	if (xcb_connection_has_error(conn))
	{
		fprintf(stderr, "Error opening display.\n");
		return 1;
	}

	const xcb_setup_t* setup;
	setup = xcb_get_setup(conn);
	screen = xcb_setup_roots_iterator(setup).data;

	signal(SIGHUP, sig_handler);
	signal(SIGINT, sig_handler);
	signal(SIGALRM, sig_handler);
	signal(SIGTERM, sig_handler);
	signal(SIGUSR1, sig_handler);
	signal(SIGUSR2, sig_handler);

	running = 1;
	xcb_cursor_context_t* cursor_ctx;
	xcb_cursor_t cursor = XCB_NONE;
	if (xcb_cursor_context_new(conn, screen, &cursor_ctx) >= 0)
	{
		cursor = xcb_cursor_load_cursor(cursor_ctx, "crosshair");
		free(cursor_ctx);
	}
	const static uint32_t event_mask[] = { XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE | XCB_EVENT_MASK_POINTER_MOTION };
	if (grab_pointer)
	{
		xcb_grab_pointer_cookie_t cookie;
		xcb_grab_pointer_reply_t *reply;
		cookie = xcb_grab_pointer(
				conn,
				0,                /* do not report events */
				screen->root,        /* grab the root window */
				*event_mask,
				XCB_GRAB_MODE_SYNC,
				XCB_GRAB_MODE_ASYNC,
				XCB_NONE,
				cursor,
				XCB_CURRENT_TIME
				);
		if ((reply = xcb_grab_pointer_reply(conn, cookie, NULL))) {
			switch (reply->status)
			{
				case XCB_GRAB_STATUS_SUCCESS:
					// OK
					break;
				case XCB_GRAB_STATUS_ALREADY_GRABBED:
					fprintf(stderr, "grab pointer already_grabbed\n");
					break;
				case XCB_GRAB_STATUS_FROZEN:
					fprintf(stderr, "grab pointer frozen\n");
					break;
				default:
					fprintf(stderr, "grab pointer error\n");
			}

			if (reply->status != XCB_GRAB_STATUS_SUCCESS)
				running = 0;

			free(reply);
		}
	}
	else
	{
		xcb_grab_button(conn,
				0,
				screen->root,
				*event_mask,
				XCB_GRAB_MODE_SYNC,
				XCB_GRAB_MODE_ASYNC,
				XCB_NONE,
				cursor,
				XCB_BUTTON_INDEX_1,
				XCB_MOD_MASK_ANY
				);
	}
	xcb_flush(conn);
	struct click_info click_info;
	while (running)
	{
		xcb_allow_events(conn, XCB_ALLOW_SYNC_POINTER, XCB_CURRENT_TIME);
		xcb_flush(conn);
		xcb_generic_event_t* event = xcb_wait_for_event(conn);
		if (!event)
		{
			running = 0;
			fprintf(stderr, "No event.\n");
		}
		char is_press = 0;
		switch (event->response_type & ~0x80)
		{
			case XCB_BUTTON_PRESS:
				is_press = 1;
			case XCB_BUTTON_RELEASE: {
				xcb_button_press_event_t* press = (xcb_button_press_event_t*) event;
				if (is_press)
				{
					click_info.press_window = press->child;
					click_info.press_x = press->root_x;
					click_info.press_y = press->root_y;
					click_info.min_x = click_info.press_x;
					click_info.min_y = click_info.press_y;
					if (output_type == OUTPUT_PRESS)
					{
						print_click_press(&click_info);
						running = report_motion;
					}
				}
				else
				{
					click_info.release_window = press->child;
					click_info.release_x = press->root_x;
					click_info.release_y = press->root_y;
					if (click_info.min_x > click_info.release_x)
						click_info.min_x = click_info.release_x;
					if (click_info.min_y > click_info.release_y)
						click_info.min_y = click_info.release_y;
					click_info.width  = click_info.press_x + click_info.release_x - \
						                2 * click_info.min_x;
					click_info.height = click_info.press_y + click_info.release_y - \
						                2 * click_info.min_y;
					switch (output_type)
					{
						case OUTPUT_PRESS:
							break;
						case OUTPUT_RELEASE:
							print_click_release(&click_info);
							break;
						case OUTPUT_GEOMETRY:
							print_click_geometry(&click_info);
							break;
						case OUTPUT_JSON:
							print_click_json(&click_info);
							break;
					}
					running = 0;
				}
				break;
			}
			case XCB_MOTION_NOTIFY: {
				if (!report_motion)
					break;
				xcb_motion_notify_event_t* motion = (xcb_motion_notify_event_t*) event;
				struct motion_info motion_info = {
					.x = motion->root_x,
					.y = motion->root_y,
					.window = motion->child
				};
				if (output_type == OUTPUT_JSON)
				{
					print_motion_json(&motion_info);
				}
				else
				{
					print_motion(&motion_info);
				}
				break;
			}
			default:
				fprintf(stderr, "Event %i\n", event->response_type & ~0x80);
				break;
		}
		free(event);
	}

	if (grab_pointer)
		xcb_ungrab_pointer(
				conn,
				XCB_CURRENT_TIME
				);
	else
		xcb_ungrab_button(
				conn,
				XCB_BUTTON_INDEX_1,
				screen->root,
				XCB_MOD_MASK_ANY
				);
	return 0;
}
