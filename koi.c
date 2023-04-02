#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#define KOI_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
  /* change 1: ganti ke random number yg besar agar tdk bentrok dengan hjkl  aaaaaaaaaaaaaaaaaa  bbbbbbbbbbbb ccccccccccccccccc dddddddddddd eeeeeeeee ffffffffff ggggggggggggggg hhhhhhhhhhhhhhh iiiiiiiiiiiiii
  ARROW_UP = 'k',
  ARROW_DOWN = 'j',
  ARROW_LEFT = 'h',
  ARROW_RIGHT = 'l'
  */

  ARROW_UP = 1000,
  ARROW_DOWN,
  ARROW_LEFT,
  ARROW_RIGHT,
  PAGE_DOWN,
  PAGE_UP,
  DEL_KEY,
  HOME_KEY,
  END_KEY
};

/*** append buffer ***/
struct abuf {
  char *b;
  int len;
};

#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf *ab, const char *s, int len) {
  char *new = realloc(ab->b, ab->len + len);

  if (new == NULL)
    return;

  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len += len;
}

void abFree(struct abuf *ab) {
  free(ab->b);
}

/*** data ***/
typedef struct erow {
  int size;
  char *chars;
} erow;

struct editorConfig {
  int cx, cy;
  int screenrows;
  int screencols;
  int numrows;
  /* rowoffset: refers to what’s at the top of the screen */
  int rowoffset;
  /* coloffset: refers to what’s at the left of the screen */
  int coloffset;
  erow *row;
  struct termios orig_termios;
};

struct editorConfig E;

void editorScroll() {
  if (E.cy < E.rowoffset) {
    E.rowoffset = E.cy;
  }

  if (E.cy >= E.rowoffset + E.screenrows) {
    E.rowoffset = E.cy - E.screenrows + 1;
  }

  /* kondisi setelah cursor pindah kekanan sehingga offset berubah kemudian kekiri sampai < columnt offset */
  if (E.cx < E.coloffset) {
    E.coloffset = E.cx;
  }

  if (E.cx >= E.coloffset + E.screencols) {
    E.coloffset = E.cx - E.screencols + 1;
  }
}

void editorDrawRows(struct abuf *b) {
  for (int y=0; y<E.screenrows; y++) {
    int filerow = y + E.rowoffset;
    if (filerow >= E.numrows) {
      if (E.numrows == 0 && y == E.screenrows/3) {
        char welcome[80];
        int welcome_len = snprintf(welcome, sizeof(welcome), "Koi editor -- version %s", KOI_VERSION);
        if (welcome_len > E.screencols)
          welcome_len = E.screencols;

        int padding = (E.screencols - welcome_len) / 2;
        if (padding > 0) {
          abAppend(b, "~", 1);
          padding--;
        }

        while (padding > 0) {
          abAppend(b, " ", 1);
          padding--;
        }

        abAppend(b, welcome, welcome_len);
      } else {
        /* write(STDOUT_FILENO, "~", 1); -> change 1: ganti ke abAppend */
        abAppend(b, "~", 1);
      }
    } else {
      int len = E.row[filerow].size - E.coloffset;
      if (len < 0) len = 0;
      if (len > E.screencols)
        len = E.screencols;

      abAppend(b, &E.row[filerow].chars[E.coloffset], len);
    }

    /* K command = Hapus setiap line. Digunankan untuk refresh line sebelum ditulis lagi.
     *             Yg dihapus adalah seluruh bagian dr line yg berada dikanan cursor/`~`
     * parameters = 0 hapus seluruh bagian dr line yg berada dikanan cursor (default)
     *              1 hapus seluruh bagian dr line yg berada dikiri cursor
     *              2 hapus 1 line penuh */
    abAppend(b, "\x1b[K", 4);
    if (y < E.screenrows - 1)
      /* write(STDOUT_FILENO, "\r\n", 2); */
      abAppend(b, "\r\n", 2);
  }
}

void editorRefreshScreen() {
  editorScroll();

  struct abuf ab = ABUF_INIT;

  /* ?25l = param untuk set cursor invisible 
   * -> cursor dihilangkan dulu agar jika ada flicker tdk kelihatan */
  abAppend(&ab, "\x1b[?25l", 6);

  /* \x1b = escape karakter */
  /* [2J = VT100 command yg digunakan untuk hapus seluruh screen */
  /* parameters = 2 adalah parameter hapus seluruh screen.
   *              0 hapus seluruh current cursor until end (default)
   *              1 hapus start until current cursor  */
  /* write(STDOUT_FILENO, "\x1b[2J", 4); -> // change 1 : ganti dengan abAppend  */
  /* abAppend(&ab, "\x1b[2J", 4); -> change 2: dihapus, ganti dengan [2K disetiap line */

  /* [H = command untuk meletakan cursor.
   * parameters = baris & kolom. dimulai dr baris 1 kolom 1 (default) */
  /* write(STDOUT_FILENO, "\x1b[H", 3); */
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);

  char buf[32];
  /* snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1); */
  /* change 1: kurangi dg E.rowoffset agar cursor bisa up jika E.cy > screenrows  */
  /* change 2: kurangi dg E.coloffset agar cursor bisa go to right jika E.cx > screenrows  */
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoffset) + 1, (E.cx - E.coloffset) + 1);
  abAppend(&ab, buf, strlen(buf));

  /* ?25h = param untuk set cursor visible
   * -> cursor ditampilkan lagi */
  abAppend(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

void editorMoveCursor(int key) {
  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];

  switch(key) {
    case ARROW_LEFT:
      if (E.cx != 0) {
        E.cx--;
      }

      break;
    case ARROW_RIGHT:
      /* change 1 */
      /* if (E.cx != E.screencols - 1) {
        E.cx++;
      } */

      if (row && E.cx < row->size) {
        E.cx++;
      }

      break;
    case ARROW_DOWN:
      /* if (E.cy < E.screenrows - 1) { */
      /* change 1: bandingkan dengan numrows karena numrows = row dr file yg sedang dibuka  */
      if (E.cy < E.numrows) {
        E.cy++;
      }

      break;
    case ARROW_UP:
      if (E.cy != 0) {
        E.cy--;
      }

      break;
  }
}

/*** terminal ***/
void die(const char *s) {
  editorRefreshScreen();

  perror(s);
  exit(1);
}

void disableRawMode() {
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    die("tcsetattr");
}

void enableRawMode() {
  tcgetattr(STDIN_FILENO, &E.orig_termios);
  atexit(disableRawMode);

  struct termios raw = E.orig_termios;
  // Miscellaneous flags
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);

  // IXON = Disable XOFF (Ctrl-s) & XONN (Ctrl-q)
  // ICRNL = disable cariage return '\r' & newline '\n'
  raw.c_iflag &= ~(ICRNL | IXON);

  // OPOST = disable output post-processing, default '\n' will translate to '\r\n' 
  raw.c_oflag &= ~(OPOST);

  // Miscellaneous flags
  raw.c_cflag |= (CS8);

  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr");
}

int editorReadKey() {
  int nread;
  char c;

  while((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    if (nread == -1 && errno != EAGAIN)
      die("read");
  }

  /* Arrow send 3bytes char '\x1b', '[' & antara 'A' 'B', 'C', 'D' misal: \x1b[A untuk UP
   * cek jika kiriman ke-1 = '\x1b' */
  if (c == '\x1b') {
    char seq[3];

    /* cek jika kiriman ke-2 = '[' */
    if (read(STDIN_FILENO, &seq[0], 1) != 1)
      return '\x1b';
    /* cek jika kiriman ke-2 = antara 'A', 'B', 'C' atau 'D' */
    if (read(STDIN_FILENO, &seq[1], 1) != 1)
      return '\x1b';

    if (seq[0] == '[') {
      /* macos:
       * PAGE_UP = fn+arrow_up 
       * PAGE_DOWN = fn+arrow_down
       * HOME = fn+arrow_left
       * END = fn+arrow_right */
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1)
          return '\x1b';

        if (seq[2] == '~') {
          switch(seq[1]) {
            case '1': return HOME_KEY;
            case '3': return DEL_KEY;
            case '4': return END_KEY;
            case '5': return PAGE_UP;
            case '6': return PAGE_DOWN;
            case '7': return HOME_KEY;
            case '8': return END_KEY;
          }
        }
      } else {
        switch(seq[1]) {
          case 'A': return ARROW_UP;
          case 'B': return ARROW_DOWN;
          case 'C': return ARROW_RIGHT;
          case 'D': return ARROW_LEFT;
          case 'H': return HOME_KEY;
          case 'F': return END_KEY;
        }
      }
    } else if (seq[0] == 'O') {
      switch(seq[1]) {
        case 'H': return HOME_KEY;
        case 'F': return END_KEY;
      }
    }

    return '\x1b';
  }

  return c;
}

void editorProcessKeycaps() {
  int c = editorReadKey();

  switch(c) {
    case CTRL_KEY('q'):
      editorRefreshScreen();

      exit(0);
      break;

    case HOME_KEY:
      E.cx = 0;
      break;
    case END_KEY:
      E.cx = E.screencols - 1;
      break;
    case PAGE_UP:
    case PAGE_DOWN:
      {
        int times = E.screenrows;
        while (times--)
          editorMoveCursor(c == PAGE_UP ? ARROW_UP: ARROW_DOWN);
      }

      break;
    case ARROW_RIGHT:
    case ARROW_DOWN:
    case ARROW_UP:
    case ARROW_LEFT:
      editorMoveCursor(c);
  }
}

int getCursorPosition(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;

  /* n = untuk meminta device status report. reply dr command ini bisa dibaca dg std input / read(STDIN_FILENO,...)
   * params: 0, 3, 5, 6
   * 6 = tolong report posisi cursor yg sedang aktif*/
  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;

  while(i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1)
      break;

    if (buf[i] == 'R')
      break;

    i++;
  }

  buf[i] = '\0';

  if (buf[0] != '\x1b' || buf[1] != '[')
    return -1;

  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
    return -1;

  return 0;
}

int getWindowSize(int *rows, int *cols) {
  struct winsize ws;

  /* TIOCGWINSZ = Terminal IOCtl (which itself stands for Input/Output Control) Get WINdow SiZe */
  if (1 || ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    /* C = untuk move right, 999C = move right 999 kolom
     * B = untuk move down , 999B = move bottom 999 baris */
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12)
      return -1;

    return getCursorPosition(rows, cols);
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;

    return 0;
  }
}

void editorAppendRow(char *line, size_t len) {
  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

  int at = E.numrows;
  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, line, len);
  E.row[at].chars[len] = '\0';
  E.numrows++;
}

/*** file i/o ***/
void editorOpen(char *filename) {
  FILE *file = fopen(filename, "r");
  if (!file)
    die("fopen");

  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;

  while((linelen = getline(&line, &linecap, file)) != -1) {
    while (linelen > 0 && (line[linelen-1] == '\n' || line[linelen-1] == '\r'))
      linelen--;
    
    editorAppendRow(line, linelen);
  }


  free(line);
  fclose(file);
}

/*** init ***/
void initEditor() {
  E.cx = 0;
  E.cy = 0;
  E.numrows = 0;
  E.rowoffset = 0;
  E.coloffset = 0;
  E.row = NULL;

  if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main(int argc, char *argv[]) {
  enableRawMode();
  initEditor();
  
  if (argc >= 2)
    editorOpen(argv[1]);

  while(1) {
    /* after refator */
    editorRefreshScreen();
    editorProcessKeycaps();

    /* before refactor
    char c = '\0';


    if (iscntrl(c)) {
      // \r = send cursor to beginning of line
      // \n = send cursor to newline
      printf("%d\r\n", c);
    } else {
      printf("%d ('%c')\r\n", c, c);
    }

    if (c == CTRL_KEY('q')) break;
    */
  }

  return 0;
}
