#define ENTRY_NAME	0
#define HOST		1
#define PORT		2
#define USERNAME	3
#define DISPLAY_SIZE	4
#define GATEWAY		5
#define GW_PORT		6
#define GW_USER		7
#define EXTRA_ARGS	8
#define NUM_FIELDS	9
#define MAX_ENTRIES	250
#define ADD_MODE	0
#define EDIT_MODE	1
#define DEFAULT_SIZE	"1680x950"

typedef struct {
    char	*fields[NUM_FIELDS];
} entry_t;

extern int save_entries(char *entries_file, entry_t *entries, int num_entries);
extern int load_entries(char *entries_file, entry_t *entries);
extern void alert(const char *fmt, ...);
extern int alltrim(char *string);

extern char *widget_names[];
