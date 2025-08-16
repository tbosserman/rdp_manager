#define ENTRY_NAME	0
#define HOST		1
#define PORT		2
#define DOMAIN		3
#define USERNAME	4
#define DISPLAY_SIZE	5
#define MULTI_MONITOR	6
#define GATEWAY		7
#define GW_PORT		8
#define GW_USER		9
#define NUM_FIELDS	10
#define MAX_ENTRIES	250
#define ADD_MODE	0
#define EDIT_MODE	1
#define DEFAULT_SIZE	""
//#define DEFAULT_SIZE	"1680x950"
//#define XFREERDP	"/opt/freerdp/bin/xfreerdp"
#define XFREERDP	"/usr/bin/xfreerdp"

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

// Global options values
#define LOCAL		0
#define REMOTE		1

typedef struct {
    char	*fields[NUM_FIELDS];
} entry_t;

// A bit insane that I'm creating a struct for this, but we'll probably
// think of other global options as time goes by....
typedef struct {
    int		access_mode; /* REMOTE or LOCAL */
} options_t;

extern int save_entries(char *entries_file, entry_t *entries, int num_entries);
extern int load_entries(char *entries_file, entry_t *entries);
extern void alert(const char *fmt, ...);
extern int alltrim(char *string);

extern char *widget_names[];
