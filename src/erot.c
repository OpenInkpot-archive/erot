#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include <Ecore.h>
#include <Ecore_X.h>
#include <Ecore_Con.h>

#ifndef DATADIR
#define DATADIR "."
#endif

#define ROTATE "Rotate"

void rotate();

void exit_all(void* param) { ecore_main_loop_quit(); }

static void die(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

typedef struct
{
    char* msg;
    int size;
} client_data_t;

static int _client_add(void* param, int ev_type, void* ev)
{
    Ecore_Con_Event_Client_Add* e = ev;
    client_data_t* msg = malloc(sizeof(client_data_t));
    msg->msg = strdup("");
    msg->size = 0;
    ecore_con_client_data_set(e->client, msg);
    return 0;
}

static int _client_del(void* param, int ev_type, void* ev)
{
    Ecore_Con_Event_Client_Del* e = ev;
    client_data_t* msg = ecore_con_client_data_get(e->client);

    /* Handle */
	if(strlen(ROTATE) == msg->size && !strncmp(ROTATE, msg->msg, msg->size))
		rotate();

    //printf(": %.*s(%d)\n", msg->size, msg->msg, msg->size);

    free(msg->msg);
    free(msg);
    return 0;
}

static int _client_data(void* param, int ev_type, void* ev)
{
    Ecore_Con_Event_Client_Data* e = ev;
    client_data_t* msg = ecore_con_client_data_get(e->client);
    msg->msg = realloc(msg->msg, msg->size + e->size);
    memcpy(msg->msg + msg->size, e->data, e->size);
    msg->size += e->size;
    return 0;
}

void rotate()
{
	Ecore_X_Randr_Rotation r;
	Ecore_X_Window root;

	root = ecore_x_window_root_first_get();

	ecore_x_randr_get_screen_info_prefetch(root);
	ecore_x_randr_get_screen_info_fetch();
	r = ecore_x_randr_screen_rotation_get(root);

	switch(r) {
		case ECORE_X_RANDR_ROT_0:
			r = ECORE_X_RANDR_ROT_90;
			break;
		case ECORE_X_RANDR_ROT_90:
			r = ECORE_X_RANDR_ROT_180;
			break;
		case ECORE_X_RANDR_ROT_180:
			r = ECORE_X_RANDR_ROT_270;
			break;
		case ECORE_X_RANDR_ROT_270:
			r = ECORE_X_RANDR_ROT_0;
			break;
		default:
			r = ECORE_X_RANDR_ROT_0;
			break;
	}

	ecore_x_randr_get_screen_info_prefetch(root);
	ecore_x_randr_get_screen_info_fetch();
	ecore_x_randr_screen_rotation_set(root, r);
}

int main(int argc, char **argv)
{
	if(!ecore_init())
		die("Unable to initialize Ecore\n");
	if(!ecore_con_init())
		die("Unable to initialize Ecore_Con\n");
	if(!ecore_x_init(0))
		die("Unable to initialize Ecore_X\n");

	ecore_x_io_error_handler_set(exit_all, NULL);

	ecore_con_server_add(ECORE_CON_LOCAL_USER, "erot", 0, NULL);

	ecore_event_handler_add(ECORE_CON_EVENT_CLIENT_ADD, _client_add, NULL);
	ecore_event_handler_add(ECORE_CON_EVENT_CLIENT_DATA, _client_data, NULL);
	ecore_event_handler_add(ECORE_CON_EVENT_CLIENT_DEL, _client_del, NULL);

	ecore_main_loop_begin();

	ecore_x_shutdown();
	ecore_con_shutdown();
	ecore_shutdown();

	return 0;
}
