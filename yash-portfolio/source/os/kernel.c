/* kernel.c - YD-OS "Blueprint" edition
 * Bare-metal 32-bit x86 kernel. No libc, no BIOS calls after boot.
 * Talks to hardware directly: VGA text buffer + DAC palette, PS/2 keyboard,
 * PIT timer + PC speaker, CMOS real-time clock, CPUID.
 */
typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

extern char __bss_start[], __bss_end[];

/* ---------- tiny libc (the compiler may emit calls to these) ---------- */
void *memset(void *d, int c, u32 n) { u8 *p = d; while (n--) *p++ = (u8)c; return d; }
void *memcpy(void *d, const void *s, u32 n) { u8 *p = d; const u8 *q = s; while (n--) *p++ = *q++; return d; }
static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static int streq(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }

/* ---------- port I/O ---------- */
static inline u8 inb(u16 p) { u8 r; __asm__ volatile("inb %1,%0" : "=a"(r) : "Nd"(p)); return r; }
static inline void outb(u16 p, u8 v) { __asm__ volatile("outb %0,%1" : : "a"(v), "Nd"(p)); }

/* ---------- blueprint palette (loaded into the VGA DAC at boot) ---------- */
enum { CBG, CBAR, CSEL, CCHALK, CYEL, CCYAN, CLINE, CTEXT,      /* 0-7: usable as backgrounds */
       CDIM, CRED, CMINT, CPINK, CORNG, CWHITE, CINK, CMID };   /* 8-15: foreground only      */
#define A(bg, fg) ((u8)(((bg) << 4) | (fg)))
#define NORM A(CBG, CTEXT)
static const u8 PAL[16][3] = {
    {0x0c,0x2d,0x7a},{0x08,0x1f,0x57},{0x1f,0x4f,0xb5},{0xea,0xf1,0xff},
    {0xff,0xe1,0x4d},{0x7f,0xe3,0xff},{0x4d,0x78,0xd1},{0xb9,0xcc,0xf5},
    {0x7f,0x9b,0xe0},{0xff,0x7a,0x6b},{0x7d,0xff,0xb3},{0xff,0x9e,0xe0},
    {0xff,0xb3,0x47},{0xff,0xff,0xff},{0x05,0x12,0x3a},{0x3a,0x66,0xc9}};

#define PX 26          /* panel text origin */
#define PW 52          /* panel text width  */
#define CY0 4          /* first content row */
#define CY1 22         /* last content row  */

/* ---------- VGA text mode ---------- */
#define VGA ((volatile u16 *)0xB8000)
static void put(int x, int y, u8 c, u8 a) { if ((unsigned)x < 80 && (unsigned)y < 25) VGA[y * 80 + x] = (u16)(a << 8) | c; }
static int text(int x, int y, const char *s, u8 a) { int n = 0; while (*s) { put(x + n, y, (u8)*s++, a); n++; } return n; }
static void fill(int x, int y, int w, int h, u8 c, u8 a) { for (int j = 0; j < h; j++) for (int i = 0; i < w; i++) put(x + i, y + j, c, a); }
static void frame(int x, int y, int w, int h, u8 a) {
    for (int i = 1; i < w - 1; i++) { put(x + i, y, 0xC4, a); put(x + i, y + h - 1, 0xC4, a); }
    for (int j = 1; j < h - 1; j++) { put(x, y + j, 0xB3, a); put(x + w - 1, y + j, 0xB3, a); }
    put(x, y, 0xDA, a); put(x + w - 1, y, 0xBF, a); put(x, y + h - 1, 0xC0, a); put(x + w - 1, y + h - 1, 0xD9, a);
}
static void utoa(u32 v, char *b) { char t[12]; int n = 0, i = 0; do { t[n++] = '0' + v % 10; v /= 10; } while (v); while (n) b[i++] = t[--n]; b[i] = 0; }
static void num(int x, int y, u32 v, int width, u8 a) {   /* zero padded */
    char b[12]; utoa(v, b); int l = slen(b);
    for (int i = 0; i < width; i++) put(x + i, y, i < width - l ? '0' : (u8)b[i - (width - l)], a);
}
static void scat(char *d, const char *a, const char *b, const char *c) {
    while (*a) *d++ = *a++;
    while (*b) *d++ = *b++;
    while (*c) *d++ = *c++;
    *d = 0;
}
/* word-wrapped paragraph; returns rows used */
static int wrap(int x, int y, int w, const char *s, u8 a) {
    int row = 0;
    while (*s) {
        while (*s == ' ') s++;
        if (!*s) break;
        int len = 0, last = -1;
        while (s[len] && len < w) { if (s[len] == ' ') last = len; len++; }
        if (s[len] && s[len] != ' ' && last > 0) len = last;
        if (y + row > CY1) break;
        for (int i = 0; i < len; i++) put(x + i, y + row, (u8)s[i], a);
        s += len; row++;
    }
    return row;
}
static int bullet(int x, int y, int w, const char *s, u8 a) { put(x, y, 0xFE, A(CBG, CCYAN)); return wrap(x + 2, y, w - 2, s, a); }

static void vga_init(void) {
    inb(0x3DA);                                              /* attribute palette: identity map */
    for (int i = 0; i < 16; i++) { outb(0x3C0, (u8)i); outb(0x3C0, (u8)i); }
    inb(0x3DA); outb(0x3C0, 0x30); { u8 v = inb(0x3C1); outb(0x3C0, v & ~0x08); }   /* blink off: 16 bg colours */
    inb(0x3DA); outb(0x3C0, 0x20);
    for (int i = 0; i < 16; i++) {                           /* DAC colours, 6 bits per channel */
        outb(0x3C8, (u8)i);
        outb(0x3C9, PAL[i][0] >> 2); outb(0x3C9, PAL[i][1] >> 2); outb(0x3C9, PAL[i][2] >> 2);
    }
    outb(0x3D4, 0x0A); outb(0x3D5, 0x20);                    /* hide hardware cursor */
}

/* ---------- time: PIT channel 0 polled as a millisecond clock ---------- */
static u32 g_now, ms_acc; static u16 pit_last;
static u32 millis(void) {
    outb(0x43, 0x00);
    u16 c = inb(0x40); c |= (u16)inb(0x40) << 8;
    u16 d = (u16)(pit_last - c); pit_last = c;
    ms_acc += d;
    while (ms_acc >= 1193) { ms_acc -= 1193; g_now++; }
    return g_now;
}
static void pit_init(void) { outb(0x43, 0x34); outb(0x40, 0); outb(0x40, 0); pit_last = 0; }

/* ---------- PC speaker (non-blocking) ---------- */
static const u16 *mel; static int mel_n, mel_i, snd_on; static u32 snd_end;
static void spk(u32 hz) {
    if (hz) { u32 d = 1193182 / hz; outb(0x43, 0xB6); outb(0x42, d & 0xFF); outb(0x42, (d >> 8) & 0xFF); outb(0x61, inb(0x61) | 3); }
    else outb(0x61, inb(0x61) & ~3);
}
static void tone(u32 hz, u32 ms) { mel = 0; spk(hz); snd_end = g_now + ms; snd_on = 1; }
static void play(const u16 *m, int n) { mel = m; mel_n = n; mel_i = 0; snd_on = 1; snd_end = 0; }
static void sound_tick(u32 now) {
    if (!snd_on || now < snd_end) return;
    if (mel && mel_i < mel_n) { spk(mel[mel_i * 2]); snd_end = now + mel[mel_i * 2 + 1]; mel_i++; }
    else { spk(0); snd_on = 0; mel = 0; }
}
static const u16 M_CHIME[]  = { 392,70, 523,70, 659,110 };
static const u16 M_HIRE[]   = { 523,90, 659,90, 784,90, 1047,90, 784,90, 1047,260 };
static const u16 M_DIE[]    = { 392,110, 330,110, 262,260 };
static const u16 M_SCALE[]  = { 262,90, 294,90, 330,90, 349,90, 392,90, 440,90, 494,90, 523,200 };

/* ---------- keyboard: PS/2 set 1 with Shift / Caps ---------- */
enum { K_UP = 256, K_DOWN, K_LEFT, K_RIGHT, K_ENTER, K_ESC, K_BACK, K_TAB };
static const char SC_N[] = "\0\0" "1234567890-=" "\0\0" "qwertyuiop[]" "\0\0" "asdfghjkl;'`" "\0" "\\" "zxcvbnm,./" "\0" "*" "\0" " ";
static const char SC_S[] = "\0\0" "!@#$%^&*()_+" "\0\0" "QWERTYUIOP{}" "\0\0" "ASDFGHJKL:\"~" "\0" "|" "ZXCVBNM<>?" "\0" "*" "\0" " ";
static int kb_ext, kb_shift, kb_caps; static u8 last_sc;
static int kb_poll(void) {
    u8 st = inb(0x64);
    if (!(st & 1)) return 0;
    u8 sc = inb(0x60);
    if (st & 0x20) return 0;                                  /* mouse byte */
    last_sc = sc;
    if (sc == 0xE0) { kb_ext = 1; return 0; }
    int rel = sc & 0x80; sc &= 0x7F;
    int ext = kb_ext; kb_ext = 0;
    if (sc == 0x2A || sc == 0x36) { kb_shift = !rel; return 0; }
    if (rel) return 0;
    if (sc == 0x3A) { kb_caps ^= 1; return 0; }
    if (ext) {
        switch (sc) { case 0x48: return K_UP; case 0x50: return K_DOWN; case 0x4B: return K_LEFT;
                      case 0x4D: return K_RIGHT; case 0x1C: return K_ENTER; }
        return 0;
    }
    switch (sc) { case 0x01: return K_ESC; case 0x1C: return K_ENTER; case 0x0E: return K_BACK; case 0x0F: return K_TAB; }
    if (sc < 58) {
        char c = (kb_shift ? SC_S : SC_N)[sc];
        if (!c) return 0;
        if (kb_caps) { if (c >= 'a' && c <= 'z') c -= 32; else if (c >= 'A' && c <= 'Z') c += 32; }
        return (u8)c;
    }
    return 0;
}

/* ---------- RTC, CPUID, RNG ---------- */
static u8 cmos(u8 r) { outb(0x70, r); return inb(0x71); }
static void rtc_get(int *h, int *m, int *s, int *d, int *mo, int *y) {
    for (int i = 0; i < 20000 && (cmos(0x0A) & 0x80); i++) { }
    u8 b = cmos(0x0B);
#define BCD(v) ((b & 4) ? (v) : (((v) & 15) + ((v) >> 4) * 10))
    *s = BCD(cmos(0)); *m = BCD(cmos(2)); *h = BCD(cmos(4));
    *d = BCD(cmos(7)); *mo = BCD(cmos(8)); *y = 2000 + BCD(cmos(9));
}
static void cpu_vendor(char *o) {
    u32 a, b, c, d;
    __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "0"(0));
    memcpy(o, &b, 4); memcpy(o + 4, &d, 4); memcpy(o + 8, &c, 4); o[12] = 0;
}
static u32 rs = 2463534242u;
static u32 rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }

/* ========================================================================
 *  RESUME DATA  (source: my_resume.pdf)
 * ====================================================================== */
typedef struct { const char *co, *role, *when, *where, *b[5]; } Job;
static const Job JOBS[3] = {
  { "SmartQ", "Software Engineer", "Jun 2022 - Present", "Bengaluru, India", {
      "Led frontend development for large-scale enterprise products using React and TypeScript.",
      "Improved application performance and load times through code optimization and architecture refinements.",
      "Collaborated with product, backend, and design teams to deliver scalable user-facing features.",
      "Built reusable UI components and design patterns adopted across teams.",
      "Contributed to system design discussions, improving reliability and maintainability." } },
  { "IntelliMedia Networks", "Full Stack Developer", "Apr 2021 - Jun 2022", "Ahmedabad, India", {
      "Developed full-stack features using React, Node.js, and AWS services.",
      "Designed REST APIs and improved frontend-backend integration efficiency.",
      "Delivered production-ready features with focus on performance and usability.", 0, 0 } },
  { "Xelpmoc Design & Tech", "Full Stack Engineer", "Jan 2020 - Apr 2021", "Bengaluru, India", {
      "Built scalable web applications and reusable frontend modules.",
      "Worked across the stack delivering UI, APIs, and deployment workflows.", 0, 0, 0 } } };
static const char *SKL[6][2] = {
  { "Languages", "TypeScript, JavaScript, HTML, CSS, C++, SQL" },
  { "Frontend",  "React.js, Next.js, Redux, Web Performance, Accessibility, Responsive UI" },
  { "Backend",   "Node.js, Express.js, REST APIs, Serverless Architecture" },
  { "Cloud",     "AWS (Lambda, S3), CI/CD, Microservices Basics" },
  { "Tools",     "Git, Webpack, Electron.js, Agile, System Design Fundamentals" },
  { "Concepts",  "Scalability, Performance Optimization, Clean Architecture, Design Patterns" } };
static const char *SUM1 = "Software Engineer with 5+ years of experience building scalable, high-performance web applications using React, TypeScript, and Node.js. Proven track record of delivering production-grade systems, improving performance, and driving frontend architecture decisions.";
static const char *SUM2 = "Experienced in AWS, system design fundamentals, and cross-functional collaboration to ship user-centric products at scale.";
static const char *IMPACT[5] = {
  "Delivered multiple production-grade React applications used by enterprise users.",
  "Drove frontend architecture improvements and reusable component strategies.",
  "Contributed to performance optimization initiatives across applications.",
  "Expanded from frontend specialist to full-stack engineer with cloud exposure.",
  "Mentored peers and contributed to engineering best practices." };
static const char *CERTS[4] = { "Networking Administrator", "CutShort Certified JavaScript (Advanced)", "CutShort Certified HTML/CSS", "Generative AI Tools" };
static const char *EARLY[3] = { "Software Engineer - Ruby Infotech", "Software Engineer Intern - Indian Oil Corporation Limited", "Team Lead (Programming) - NTC Robocon" };
static const char *PROJ[3][3] = {
  { "Bootable portfolio OS", "", "This disk. An x86 assembly bootloader and a 32-bit kernel in C, with a menu UI, shell, offline Q&A, Snake and Breakout." },
  { "Web OS", "Aug 2021 - Jan 2023", "A desktop environment in the browser, built to turn low-resource devices such as a Raspberry Pi into interactive desktops." },
  { "3D chatbot portfolio", "Dec 2023 - Present", "A Three.js portfolio you explore in 3D, combined with a conversational AI chatbot." } };

/* ========================================================================
 *  UI STATE & CHROME
 * ====================================================================== */
#define NITEMS 11
enum { F_MENU, F_APP };
enum { G_READY, G_RUN, G_PAUSE, G_OVER };
static const char *ITEMS[NITEMS] = { "About", "Experience", "Skills", "Education", "Projects", "Contact",
                                     "Terminal", "Ask Yash", "System", "Snake", "Breakout" };
static const u8 NPAGES[NITEMS] = { 2, 3, 1, 1, 1, 1, 0, 0, 0, 0, 0 };
static const char *TITLES[NITEMS] = { "ABOUT", "EXPERIENCE", "SKILLS", "EDUCATION AND MORE", "PROJECTS", "CONTACT",
                                      "TERMINAL", "ASK YASH  (offline Q&A)", "SYSTEM", "SNAKE", "BREAKOUT" };
static int sel, page, focus;
static u32 clock_next, sys_next, blink_phase = 99;

static void draw_clock(void) {
    int h, m, s, d, mo, y; rtc_get(&h, &m, &s, &d, &mo, &y);
    u8 a = A(CBAR, CCYAN);
    num(71, 0, h, 2, a); put(73, 0, ':', a); num(74, 0, m, 2, a); put(76, 0, ':', a); num(77, 0, s, 2, a);
}
static void draw_topbar(void) {
    fill(0, 0, 80, 1, ' ', A(CBAR, CCHALK));
    text(2, 0, "YD-OS 1.0", A(CBAR, CYEL));
    text(13, 0, "|  Yash Datir  |  Senior Software Engineer", A(CBAR, CCHALK));
    draw_clock();
}
static const char *hint(void) {
    if (focus == F_MENU) return " Up/Down: browse   Left/Right: page   Enter: open app   1-9: jump";
    switch (sel) {
        case 6: return " Type a command + Enter   Up: history   Esc: back to menu";
        case 7: return " Type a question + Enter   Esc: back to menu";
        case 8: return " Press any key to test the keyboard   Esc: back to menu";
        case 9: return " Arrows or WASD: steer   P: pause   Esc: back to menu";
        default: return " Left/Right or A/D: paddle   Space: launch   P: pause   Esc: back";
    }
}
static void draw_bottom(void) {
    fill(0, 24, 80, 1, ' ', A(CCHALK, CINK));
    text(0, 24, hint(), A(CCHALK, CINK));
    text(71, 24, "[", A(CCHALK, CINK)); num(72, 24, sel + 1, 2, A(CCHALK, CBG));
    text(74, 24, "/11]", A(CCHALK, CINK));
}
static int item_y(int i) { return i < 6 ? 4 + i : 12 + (i - 6); }
static void draw_sidebar(void) {
    fill(1, 2, 22, 21, ' ', NORM);
    text(3, 3, "RESUME", A(CBG, CDIM));
    text(3, 11, "APPS", A(CBG, CDIM));
    for (int i = 0; i < NITEMS; i++) {
        int y = item_y(i);
        if (i == sel) {
            u8 a = focus == F_APP ? A(CYEL, CINK) : A(CSEL, CWHITE);
            fill(1, y, 22, 1, ' ', a);
            put(2, y, 0x10, a);
            text(4, y, ITEMS[i], a);
        } else text(4, y, ITEMS[i], NORM);
    }
    text(3, 19, "Esc", A(CBG, CYEL)); text(7, 19, "back to menu", A(CBG, CDIM));
    text(3, 20, "Tab", A(CBG, CYEL)); text(7, 20, "next item", A(CBG, CDIM));
    text(3, 22, "Bare-metal x86", A(CBG, CLINE));
}
static void draw_static_frames(void) {
    fill(0, 1, 80, 23, ' ', NORM);
    frame(0, 1, 24, 23, A(CBG, CLINE));
    frame(24, 1, 56, 23, A(CBG, CLINE));
}
static void panel_clear(void) { fill(25, 2, 54, 21, ' ', NORM); }
static void panel_head(void) {
    panel_clear();
    text(PX, 2, TITLES[sel], A(CBG, CYEL));
    for (int i = 0; i < PW; i++) put(PX + i, 3, 0xC4, A(CBG, CLINE));
    if (NPAGES[sel] > 1) {
        text(69, 2, "<", A(CBG, CCYAN)); put(71, 2, '0' + page + 1, A(CBG, CWHITE));
        put(72, 2, '/', A(CBG, CDIM)); put(73, 2, '0' + NPAGES[sel], A(CBG, CDIM)); text(75, 2, ">", A(CBG, CCYAN));
    }
}

/* ---------- resume pages ---------- */
static void page_about(void) {
    if (page == 0) {
        text(PX, 5, "Yash Datir", A(CBG, CWHITE));
        text(PX, 6, "Senior Software Engineer", A(CBG, CYEL));
        text(PX, 7, "Frontend & Full Stack  |  Bengaluru, India", A(CBG, CCYAN));
        int x = PX;
        x += text(x, 9, " 5+ years ", A(CYEL, CINK)) + 1;
        x += text(x, 9, " React ", A(CCHALK, CINK)) + 1;
        x += text(x, 9, " TypeScript ", A(CCHALK, CINK)) + 1;
        x += text(x, 9, " Node.js ", A(CCHALK, CINK)) + 1;
        text(x, 9, " AWS ", A(CCHALK, CINK));
        int r = wrap(PX, 11, PW, SUM1, NORM);
        wrap(PX, 12 + r, PW, SUM2, NORM);
    } else {
        text(PX, 5, "Key impact", A(CBG, CWHITE));
        int y = 7;
        for (int i = 0; i < 5; i++) y += bullet(PX, y, PW, IMPACT[i], NORM) + 1;
    }
}
static void page_experience(void) {
    const Job *j = &JOBS[page];
    text(PX, 5, j->co, A(CBG, CYEL));
    text(PX, 6, j->role, A(CBG, CWHITE));
    int n = text(PX, 7, j->when, A(CBG, CCYAN)); text(PX + n, 7, "  |  ", A(CBG, CLINE)); text(PX + n + 5, 7, j->where, A(CBG, CDIM));
    int y = 9;
    for (int i = 0; i < 5 && j->b[i]; i++) y += bullet(PX, y, PW, j->b[i], NORM);
}
static void page_skills(void) {
    int y = 5;
    for (int i = 0; i < 6; i++) {
        text(PX, y, SKL[i][0], A(CBG, CCYAN));
        int r = wrap(PX + 11, y, PW - 11, SKL[i][1], NORM);
        y += (r < 1 ? 1 : r) + 1;
    }
}
static void page_education(void) {
    text(PX, 5, "B.E. in Computer Engineering", A(CBG, CWHITE));
    text(PX, 6, "Neotech Institute of Technology", NORM);
    text(PX, 7, "2016 - 2020", A(CBG, CDIM));
    text(PX, 9, "Certifications", A(CBG, CYEL));
    int y = 10;
    for (int i = 0; i < 4; i++) y += bullet(PX, y, PW, CERTS[i], NORM);
    text(PX, y + 1, "Early career", A(CBG, CYEL));
    y += 2;
    for (int i = 0; i < 3; i++) y += bullet(PX, y, PW, EARLY[i], NORM);
}
static void page_projects(void) {
    int y = 5;
    for (int i = 0; i < 3; i++) {
        int n = text(PX, y, PROJ[i][0], A(CBG, CWHITE));
        if (PROJ[i][1][0]) text(PX + n + 2, y, PROJ[i][1], A(CBG, CCYAN));
        y += 1 + wrap(PX, y + 1, PW, PROJ[i][2], NORM) + 1;
    }
    text(PX, y, "More: yashdatir.github.io/Yash-Datir", A(CBG, CDIM));
}
static void page_contact(void) {
    static const char *L[5][2] = { { "Location", "Bengaluru, India" }, { "Phone", "+91-7990097420" },
        { "Email", "yashdatir@gmail.com" }, { "LinkedIn", "linkedin.com/in/yash-datir" },
        { "Portfolio", "yashdatir.github.io/Yash-Datir" } };
    for (int i = 0; i < 5; i++) { text(PX, 5 + i * 2, L[i][0], A(CBG, CCYAN)); text(PX + 11, 5 + i * 2, L[i][1], A(CBG, CWHITE)); }
    text(PX, 17, "Open the Terminal and type:", A(CBG, CDIM));
    text(PX, 18, " sudo hire ", A(CYEL, CINK));
}

/* ========================================================================
 *  APP: TERMINAL
 * ====================================================================== */
#define TW 52
#define TL 80
static char tl[TL][TW + 1]; static u8 ta[TL]; static int tn;
static char tin[40]; static int tlen;
static char thist[8][40]; static int thn, thpos;
static int term_inited;
static void tpush(const char *s, int len, u8 a) {
    if (tn == TL) { memcpy(tl[0], tl[1], (TL - 1) * (TW + 1)); memcpy(ta, ta + 1, TL - 1); tn--; }
    memcpy(tl[tn], s, len); tl[tn][len] = 0; ta[tn] = a; tn++;
}
static void tadd_i(int ind, const char *s, u8 a) {
    char buf[TW + 1]; int w = TW - ind;
    while (*s) {
        while (*s == ' ') s++;
        if (!*s) break;
        int len = 0, last = -1;
        while (s[len] && len < w) { if (s[len] == ' ') last = len; len++; }
        if (s[len] && s[len] != ' ' && last > 0) len = last;
        for (int i = 0; i < ind; i++) buf[i] = ' ';
        memcpy(buf + ind, s, len); tpush(buf, ind + len, a); s += len;
    }
}
#define tadd(s, a) tadd_i(0, (s), (a))
#define tblank() tpush("", 0, 0)
static void term_cursor(u32 phase) { put(PX + 15 + tlen, CY1, phase ? '_' : ' ', A(CBG, CWHITE)); }
static void term_draw(void) {
    panel_head();
    int rows = CY1 - CY0;                        /* 18 output rows above the prompt */
    int n = tn < rows ? tn : rows, start = tn - n;
    for (int i = 0; i < n; i++) text(PX, CY1 - n + i, tl[start + i], ta[start + i]);
    text(PX, CY1, "guest@yd-os:~$ ", A(CBG, CMINT));
    text(PX + 15, CY1, tin, A(CBG, CWHITE));
    term_cursor(1);
}
static void t_job(const Job *j) {
    char b[80]; tadd(j->co, A(CBG, CYEL));
    scat(b, j->role, "  |  ", j->when); tadd_i(2, b, A(CBG, CWHITE));
    for (int i = 0; i < 5 && j->b[i]; i++) { scat(b, "- ", j->b[i], ""); tadd_i(2, b, NORM); }
    tblank();
}
static void run_cmd(char *line) {
    while (*line == ' ') line++;
    char *arg = line; while (*arg && *arg != ' ') arg++;
    if (*arg) { *arg++ = 0; while (*arg == ' ') arg++; }
    char *cmd = line;
    if (!*cmd) return;
    u8 W = NORM, G = A(CBG, CMINT), C = A(CBG, CCYAN), Y = A(CBG, CYEL);
    if (streq(cmd, "help")) {
        tadd("Commands:", Y);
        tadd_i(2, "about  experience  skills  education  certs  projects  contact", W);
        tadd_i(2, "date  uptime  sysinfo  whoami  ls  echo <text>  beep  play", W);
        tadd_i(2, "clear  exit  sudo hire", W);
    } else if (streq(cmd, "about")) { tadd(SUM1, W); tblank(); tadd(SUM2, W);
    } else if (streq(cmd, "experience") || streq(cmd, "exp")) { for (int i = 0; i < 3; i++) t_job(&JOBS[i]);
    } else if (streq(cmd, "skills")) {
        char b[96]; for (int i = 0; i < 6; i++) { scat(b, SKL[i][0], ": ", SKL[i][1]); tadd_i(0, b, i & 1 ? W : C); }
    } else if (streq(cmd, "education") || streq(cmd, "edu")) {
        tadd("B.E. in Computer Engineering", A(CBG, CWHITE)); tadd("Neotech Institute of Technology, 2016 - 2020", W);
    } else if (streq(cmd, "certs")) { for (int i = 0; i < 4; i++) { char b[64]; scat(b, "- ", CERTS[i], ""); tadd(b, W); }
    } else if (streq(cmd, "projects")) {
        for (int i = 0; i < 3; i++) { tadd(PROJ[i][0], Y); tadd_i(2, PROJ[i][2], W); }
    } else if (streq(cmd, "contact")) {
        tadd("Email     yashdatir@gmail.com", W); tadd("Phone     +91-7990097420", W);
        tadd("Location  Bengaluru, India", W); tadd("LinkedIn  linkedin.com/in/yash-datir", W);
        tadd("Portfolio yashdatir.github.io/Yash-Datir", W);
    } else if (streq(cmd, "date")) {
        int h, m, s, d, mo, y; rtc_get(&h, &m, &s, &d, &mo, &y);
        char b[40], t[6]; int i = 0;
        utoa(y, t); for (int k = 0; t[k]; k++) b[i++] = t[k]; b[i++] = '-';
        b[i++] = '0' + mo / 10; b[i++] = '0' + mo % 10; b[i++] = '-'; b[i++] = '0' + d / 10; b[i++] = '0' + d % 10; b[i++] = ' ';
        b[i++] = '0' + h / 10; b[i++] = '0' + h % 10; b[i++] = ':'; b[i++] = '0' + m / 10; b[i++] = '0' + m % 10; b[i++] = ':';
        b[i++] = '0' + s / 10; b[i++] = '0' + s % 10; b[i] = 0;
        char o[60]; scat(o, b, "  (read from the CMOS clock)", ""); tadd(o, W);
    } else if (streq(cmd, "uptime")) {
        u32 s = g_now / 1000; char a[12], b[12], o[48]; utoa(s / 60, a); utoa(s % 60, b);
        scat(o, "up ", a, " min "); { char o2[60]; scat(o2, o, b, " s"); tadd(o2, W); }
    } else if (streq(cmd, "sysinfo")) {
        char v[16], o[48]; cpu_vendor(v); scat(o, "CPU vendor  ", v, ""); tadd(o, W);
        tadd("Video       VGA text 80x25, custom palette", W);
        tadd("Boot        sector 0 -> 0x7C00 -> kernel at 0x10000", W);
        tadd("Mode        32-bit protected, no OS underneath", W);
    } else if (streq(cmd, "whoami")) { tadd("guest", W);
    } else if (streq(cmd, "ls")) { tadd("about  experience  skills  education  projects  contact", C);
    } else if (streq(cmd, "echo")) { tadd(arg, W);
    } else if (streq(cmd, "beep")) { tone(880, 120); tadd("beep.", W);
    } else if (streq(cmd, "play")) { play(M_SCALE, 8); tadd("Playing a C major scale on the PC speaker.", W);
    } else if (streq(cmd, "clear")) { tn = 0;
    } else if (streq(cmd, "exit")) { focus = F_MENU;
    } else if (streq(cmd, "sudo") && streq(arg, "hire")) {
        tadd("[ACCESS GRANTED] Candidate matched.", G);
        tadd("Send the offer to yashdatir@gmail.com", G);
        play(M_HIRE, 6);
    } else { char b[56]; scat(b, cmd, ": command not found. Try 'help'.", ""); tadd(b, A(CBG, CRED)); }
}
static void term_key(int k) {
    if (!term_inited) return;
    if (k == K_ENTER) {
        char echo[TW + 1]; scat(echo, "guest@yd-os:~$ ", tin, ""); tpush(echo, slen(echo) > TW ? TW : slen(echo), A(CBG, CMINT));
        if (tlen) {
            if (thn == 8) { for (int i = 1; i < 8; i++) memcpy(thist[i - 1], thist[i], 40); thn--; }
            memcpy(thist[thn++], tin, 40);
        }
        thpos = thn;
        char cmdline[40]; memcpy(cmdline, tin, 40);
        tlen = 0; tin[0] = 0;
        run_cmd(cmdline);
        if (focus == F_MENU) { draw_sidebar(); draw_bottom(); }
        term_draw();
    } else if (k == K_BACK) { if (tlen) { tin[--tlen] = 0; term_draw(); }
    } else if (k == K_UP) { if (thpos > 0) { thpos--; memcpy(tin, thist[thpos], 40); tlen = slen(tin); term_draw(); }
    } else if (k == K_DOWN) { if (thpos < thn - 1) { thpos++; memcpy(tin, thist[thpos], 40); tlen = slen(tin); } else { thpos = thn; tlen = 0; tin[0] = 0; } term_draw();
    } else if (k >= 32 && k < 127 && tlen < 34) { tin[tlen++] = (char)k; tin[tlen] = 0; term_draw(); }
}
static void term_enter(void) {
    if (!term_inited) {
        term_inited = 1;
        tadd("YD-OS shell. Type 'help' for commands.", A(CBG, CYEL));
        tadd("Try: about, skills, sysinfo, play, sudo hire", A(CBG, CDIM));
        tblank();
    }
}

/* ========================================================================
 *  APP: ASK YASH  (keyword Q&A over the resume)
 * ====================================================================== */
typedef struct { const char *keys, *ans; } QA;
static const QA QAS[] = {
 { "who yourself introduce profile summary", "Yash Datir is a Software Engineer with 5+ years of experience building scalable, high-performance web applications with React, TypeScript, and Node.js. He is based in Bengaluru, India." },
 { "year senior long level seniority", "5+ years of experience. His resume headline is Senior Software Engineer (Frontend & Full Stack); he has been a Software Engineer at SmartQ since Jun 2022." },
 { "*smartq current present now today", "At SmartQ (Jun 2022 - Present, Bengaluru) Yash led frontend development for large-scale enterprise products using React and TypeScript, improved performance and load times, and built reusable UI components adopted across teams." },
 { "*intellimedia *ahmedabad", "At IntelliMedia Networks (Apr 2021 - Jun 2022, Ahmedabad) as Full Stack Developer: full-stack features with React, Node.js and AWS, REST API design, and production-ready features focused on performance and usability." },
 { "*xelpmoc", "At Xelpmoc Design & Tech (Jan 2020 - Apr 2021, Bengaluru) as Full Stack Engineer: scalable web applications, reusable frontend modules, and work across UI, APIs and deployment workflows." },
 { "experience work job career compan employ role history", "Three roles: Software Engineer at SmartQ (Jun 2022 - Present), Full Stack Developer at IntelliMedia Networks (Apr 2021 - Jun 2022), and Full Stack Engineer at Xelpmoc Design & Tech (Jan 2020 - Apr 2021). Ask about any of them by name." },
 { "skill stack tech language react typescript javascript node next redux css sql express", "Languages: TypeScript, JavaScript, HTML, CSS, C++, SQL. Frontend: React.js, Next.js, Redux, performance, accessibility. Backend: Node.js, Express.js, REST APIs, serverless. Cloud: AWS (Lambda, S3), CI/CD. Tools: Git, Webpack, Electron.js." },
 { "aws cloud lambda serverless devops cicd", "Cloud: AWS (Lambda, S3), CI/CD and microservices basics. AWS services were used at IntelliMedia Networks, and serverless architecture is on the backend skills list." },
 { "educat degree college univers studi neotech graduat", "B.E. in Computer Engineering from Neotech Institute of Technology, 2016 - 2020." },
 { "certif course credential", "Networking Administrator; CutShort Certified JavaScript (Advanced); CutShort Certified HTML/CSS; Generative AI Tools." },
 { "project built side hobby floppy os kernel bootload assembly game snake", "Side projects: this bootable portfolio OS (x86 assembly bootloader and a C kernel), Web OS (Aug 2021 - Jan 2023), and a 3D chatbot portfolio (Dec 2023 - Present)." },
 { "contact email phone reach hire call linkedin where locat mail number", "Email: yashdatir@gmail.com. Phone: +91-7990097420. Location: Bengaluru, India. LinkedIn: linkedin.com/in/yash-datir." },
 { "perform fast speed optimi load", "At SmartQ, Yash improved application performance and load times through code optimization and architecture refinements. Web performance is on his frontend skills list." },
 { "robot intern ruby indian oil iocl team early", "Early career: Software Engineer at Ruby Infotech, Software Engineer Intern at Indian Oil Corporation Limited, and Team Lead (Programming) at NTC Robocon." },
 { "hello hey greet", "Hello! Ask about experience, skills, projects, education or contact." },
 { "mentor architect impact achiev deliver", "Key impact: delivered multiple production-grade React applications used by enterprise users, drove frontend architecture and reusable component strategies, contributed to performance work, and mentored peers." },
};
static char ain[44]; static int alen; static char aq[48]; static const char *aans; static int ask_has_q;
static void ask_cursor(u32 phase) { put(PX + 5 + alen, CY1, phase ? '_' : ' ', A(CBG, CWHITE)); }
static void ask_draw(void) {
    panel_head();
    if (!ask_has_q) {
        text(PX, 5, "Ask about Yash's experience, skills, projects,", NORM);
        text(PX, 6, "education or contact. It matches keywords against", NORM);
        text(PX, 7, "the resume. No network, no model, just C.", NORM);
        text(PX, 10, "Try:", A(CBG, CYEL));
        text(PX + 2, 11, "what is your experience?", A(CBG, CCYAN));
        text(PX + 2, 12, "which skills do you have?", A(CBG, CCYAN));
        text(PX + 2, 13, "tell me about smartq", A(CBG, CCYAN));
        text(PX + 2, 14, "how can I contact you?", A(CBG, CCYAN));
    } else {
        text(PX, 5, "You", A(CBG, CDIM)); text(PX + 4, 5, aq, A(CBG, CCYAN));
        text(PX, 7, "YD", A(CBG, CYEL));
        wrap(PX + 4, 7, PW - 4, aans, NORM);
    }
    text(PX, CY1, "ask> ", A(CBG, CYEL)); text(PX + 5, CY1, ain, A(CBG, CWHITE)); ask_cursor(1);
}
static int starts(const char *t, const char *k) { while (*k) { if (*t++ != *k++) return 0; } return 1; }
static void ask_answer(void) {
    char q[44]; int n = alen;
    for (int i = 0; i <= n; i++) { char c = ain[i]; q[i] = (c >= 'A' && c <= 'Z') ? c + 32 : c; }
    int best = -1, bscore = 0;
    for (u32 e = 0; e < sizeof(QAS) / sizeof(QAS[0]); e++) {
        int score = 0; const char *k = QAS[e].keys;
        while (*k) {
            char kw[16]; int kl = 0;
            while (*k == ' ') k++;
            while (*k && *k != ' ' && kl < 15) kw[kl++] = *k++;
            kw[kl] = 0; if (!kl) break;
            int wgt = 1; char *kp = kw; if (kw[0] == '*') { wgt = 3; kp = kw + 1; }
            const char *t = q;                                    /* does any query word start with kw? */
            while (*t) {
                while (*t && !((*t >= 'a' && *t <= 'z') || (*t >= '0' && *t <= '9'))) t++;
                if (!*t) break;
                if (starts(t, kp)) score += wgt;
                while ((*t >= 'a' && *t <= 'z') || (*t >= '0' && *t <= '9')) t++;
            }
        }
        if (score > bscore) { bscore = score; best = (int)e; }
    }
    memcpy(aq, ain, 44); aq[43] = 0;
    aans = best >= 0 ? QAS[best].ans : "I only know what is on Yash's resume. Try: experience, skills, projects, education, certifications or contact.";
    ask_has_q = 1; alen = 0; ain[0] = 0;
    tone(660, 40);
}
static void ask_key(int k) {
    if (k == K_ENTER) { if (alen) ask_answer(); ask_draw(); }
    else if (k == K_BACK) { if (alen) { ain[--alen] = 0; ask_draw(); } }
    else if (k >= 32 && k < 127 && alen < 40) { ain[alen++] = (char)k; ain[alen] = 0; ask_draw(); }
}

/* ========================================================================
 *  APP: SYSTEM  (live hardware readouts)
 * ====================================================================== */
static void sys_live(void) {
    char v[16]; cpu_vendor(v);
    fill(PX + 16, 5, 34, 4, ' ', NORM);
    text(PX + 16, 5, v, A(CBG, CWHITE));
    u32 s = g_now / 1000; num(PX + 16, 6, s / 3600, 2, A(CBG, CWHITE)); put(PX + 18, 6, ':', NORM);
    num(PX + 19, 6, (s / 60) % 60, 2, A(CBG, CWHITE)); put(PX + 21, 6, ':', NORM); num(PX + 22, 6, s % 60, 2, A(CBG, CWHITE));
    int h, m, sc, d, mo, y; rtc_get(&h, &m, &sc, &d, &mo, &y);
    num(PX + 16, 7, y, 4, A(CBG, CWHITE)); put(PX + 20, 7, '-', NORM); num(PX + 21, 7, mo, 2, A(CBG, CWHITE));
    put(PX + 23, 7, '-', NORM); num(PX + 24, 7, d, 2, A(CBG, CWHITE));
    num(PX + 27, 7, h, 2, A(CBG, CWHITE)); put(PX + 29, 7, ':', NORM); num(PX + 30, 7, m, 2, A(CBG, CWHITE));
    static const char HX[] = "0123456789ABCDEF";
    text(PX + 16, 8, "0x", A(CBG, CWHITE)); put(PX + 18, 8, HX[last_sc >> 4], A(CBG, CYEL)); put(PX + 19, 8, HX[last_sc & 15], A(CBG, CYEL));
}
static void sys_draw(void) {
    panel_head();
    text(PX, 5, "CPU vendor", A(CBG, CCYAN)); text(PX, 6, "Uptime", A(CBG, CCYAN));
    text(PX, 7, "CMOS clock", A(CBG, CCYAN)); text(PX, 8, "Last scancode", A(CBG, CCYAN));
    text(PX, 11, "Memory map", A(CBG, CYEL));
    static const char *MM[4][2] = { { "0x00007C00", "boot sector, 512 bytes" }, { "0x00010000", "this kernel, loaded by stage 1" },
                                     { "0x00090000", "stack top" }, { "0x000B8000", "VGA text buffer (what you see)" } };
    for (int i = 0; i < 4; i++) { text(PX, 12 + i, MM[i][0], A(CBG, CWHITE)); text(PX + 12, 12 + i, MM[i][1], NORM); }
    text(PX, 17, "Boot path", A(CBG, CYEL));
    text(PX, 18, "BIOS -> sector 0 -> protected mode -> kmain()", NORM);
    text(PX, 20, "Press any key: its scancode appears above.", A(CBG, CDIM));
    sys_live();
}

/* ========================================================================
 *  APP: SNAKE
 * ====================================================================== */
#define SCOLS 26
#define SROWS 19
#define SMAX  (SCOLS * SROWS)
static u8 snx[SMAX], sny[SMAX];
static int snlen, sdx, sdy, ndx, ndy, sscore, sbest, sstate, apx, apy; static u32 snext;
static void scell(int cx, int cy, u8 c, u8 a) { put(PX + cx * 2, CY0 + cy, c, a); put(PX + cx * 2 + 1, CY0 + cy, c, a); }
static void sdot(int cx, int cy) { put(PX + cx * 2, CY0 + cy, 0xFA, A(CBG, CLINE)); put(PX + cx * 2 + 1, CY0 + cy, ' ', NORM); }
static void overlay(const char *l1, const char *l2) {
    int w = 30, x = PX + (PW - w) / 2, y = 11;
    fill(x, y, w, 4, ' ', A(CCHALK, CINK));
    text(x + (w - slen(l1)) / 2, y + 1, l1, A(CCHALK, CINK));
    text(x + (w - slen(l2)) / 2, y + 2, l2, A(CCHALK, CBG));
}
static void games_head(const char *label1, int v1, const char *label2, int v2, const char *label3, int v3) {
    fill(PX, 2, PW + 1, 1, ' ', NORM);
    text(PX, 2, TITLES[sel], A(CBG, CYEL));
    int x = 52;
    if (label3) { text(x - 9, 2, label3, A(CBG, CDIM)); num(x - 9 + slen(label3), 2, v3, 1, A(CBG, CWHITE)); }
    x = 56; text(x, 2, label1, A(CBG, CDIM)); num(x + slen(label1), 2, v1, 4, A(CBG, CWHITE));
    x = 68; text(x, 2, label2, A(CBG, CDIM)); num(x + slen(label2), 2, v2, 4, A(CBG, CYEL));
}
static void snake_head(void) { games_head("Score ", sscore, "Best ", sbest, 0, 0); }
static void place_apple(void) {
    for (;;) {
        apx = rnd() % SCOLS; apy = rnd() % SROWS; int ok = 1;
        for (int i = 0; i < snlen; i++) if (snx[i] == apx && sny[i] == apy) ok = 0;
        if (ok) break;
    }
}
static void snake_reset(void) {
    snlen = 4; for (int i = 0; i < snlen; i++) { snx[i] = 10 - i; sny[i] = 9; }
    sdx = ndx = 1; sdy = ndy = 0; sscore = 0; place_apple(); sstate = G_READY;
}
static void snake_draw(void) {
    panel_head(); snake_head();
    for (int y = 0; y < SROWS; y++) for (int x = 0; x < SCOLS; x++) sdot(x, y);
    scell(apx, apy, 0xDB, A(CBG, CRED));
    for (int i = snlen - 1; i >= 0; i--) scell(snx[i], sny[i], 0xDB, A(CBG, i ? CMINT : CWHITE));
    if (sstate == G_READY) overlay("SNAKE", "Enter to start");
    else if (sstate == G_PAUSE) overlay("PAUSED", "Enter to resume");
    else if (sstate == G_OVER) overlay("GAME OVER", "Enter to play again");
}
static void snake_die(void) { sstate = G_OVER; play(M_DIE, 3); if (sscore > sbest) sbest = sscore; snake_head(); overlay("GAME OVER", "Enter to play again"); }
static void snake_step(void) {
    sdx = ndx; sdy = ndy;
    int nx = snx[0] + sdx, ny = sny[0] + sdy;
    if (nx < 0 || ny < 0 || nx >= SCOLS || ny >= SROWS) { snake_die(); return; }
    int grow = (nx == apx && ny == apy), check = snlen - (grow ? 0 : 1);
    for (int i = 0; i < check; i++) if (snx[i] == nx && sny[i] == ny) { snake_die(); return; }
    if (!grow) sdot(snx[snlen - 1], sny[snlen - 1]); else if (snlen < SMAX - 1) snlen++;
    for (int i = snlen - 1; i > 0; i--) { snx[i] = snx[i - 1]; sny[i] = sny[i - 1]; }
    snx[0] = nx; sny[0] = ny;
    if (grow) { sscore += 10; if (sscore > sbest) sbest = sscore; place_apple(); scell(apx, apy, 0xDB, A(CBG, CRED)); tone(1046, 40); snake_head(); }
    scell(snx[1], sny[1], 0xDB, A(CBG, CMINT)); scell(nx, ny, 0xDB, A(CBG, CWHITE));
    int iv = 150 - sscore; snext = g_now + (iv < 65 ? 65 : iv);
}
static void snake_key(int k) {
    if (sstate != G_RUN) {
        if (k == K_ENTER || k == ' ') { if (sstate == G_OVER) snake_reset(); sstate = G_RUN; snake_draw(); snext = g_now + 150; }
        return;
    }
    if ((k == K_UP || k == 'w') && sdy == 0) { ndx = 0; ndy = -1; }
    else if ((k == K_DOWN || k == 's') && sdy == 0) { ndx = 0; ndy = 1; }
    else if ((k == K_LEFT || k == 'a') && sdx == 0) { ndx = -1; ndy = 0; }
    else if ((k == K_RIGHT || k == 'd') && sdx == 0) { ndx = 1; ndy = 0; }
    else if (k == 'p' || k == ' ') { sstate = G_PAUSE; overlay("PAUSED", "Enter to resume"); }
}

/* ========================================================================
 *  APP: BREAKOUT
 * ====================================================================== */
#define BX0 PX
#define BX1 (PX + PW - 1)
#define BY0 CY0
#define BRY 5            /* first brick row */
#define BPY 21           /* paddle row      */
#define PADW 9
static u8 bricks[4][13];
static int bpx, bbx, bby, bdx, bdy, blives, bscore, blevel, bstate, bleft, bbest; static u32 bnext;
static void brick_draw(int r, int c, int on) {
    static const u8 COL[4] = { CRED, CORNG, CYEL, CMINT };
    for (int i = 0; i < 4; i++) put(PX + c * 4 + i, BRY + r, on && i < 3 ? 0xDB : ' ', A(CBG, COL[r]));
}
static void bricks_fill(void) { for (int r = 0; r < 4; r++) for (int c = 0; c < 13; c++) bricks[r][c] = 1; bleft = 52; }
static void ball_to_paddle(void) { bbx = bpx + PADW / 2; bby = BPY - 1; bdx = 0; bdy = 0; }
static void brk_head(void) { games_head("Score ", bscore, "Best ", bbest, "Lives ", blives); }
static void brk_reset(void) { bpx = PX + (PW - PADW) / 2; bscore = 0; blives = 3; blevel = 1; bricks_fill(); ball_to_paddle(); bstate = G_READY; }
static void paddle_draw(void) { for (int i = 0; i < PADW; i++) put(bpx + i, BPY, 0xDC, A(CBG, CCYAN)); }
static void brk_draw(void) {
    panel_head(); brk_head();
    for (int r = 0; r < 4; r++) for (int c = 0; c < 13; c++) brick_draw(r, c, bricks[r][c]);
    paddle_draw(); put(bbx, bby, 'O', A(CBG, CWHITE));
    if (bstate == G_READY) overlay("BREAKOUT", "Space to launch");
    else if (bstate == G_PAUSE) overlay("PAUSED", "Space to resume");
    else if (bstate == G_OVER) overlay("GAME OVER", "Space to play again");
}
static void brk_step(void) {
    put(bbx, bby, ' ', NORM);
    int nx = bbx + bdx, ny = bby + bdy;
    if (nx < BX0) { nx = BX0; bdx = -bdx; tone(450, 15); }
    if (nx > BX1) { nx = BX1; bdx = -bdx; tone(450, 15); }
    if (ny < BY0) { ny = BY0; bdy = 1; tone(450, 15); }
    if (ny >= BRY && ny < BRY + 4) {
        int c = (nx - PX) / 4, r = ny - BRY;
        if (bricks[r][c]) {
            bricks[r][c] = 0; brick_draw(r, c, 0); bleft--; bscore += 10; if (bscore > bbest) bbest = bscore;
            bdy = -bdy; ny = bby; tone(700 + (3 - r) * 120, 25); brk_head();
            if (!bleft) {
                blevel++; bricks_fill(); ball_to_paddle(); bstate = G_READY;
                for (int rr = 0; rr < 4; rr++) for (int cc = 0; cc < 13; cc++) brick_draw(rr, cc, 1);
                put(bbx, bby, 'O', A(CBG, CWHITE)); play(M_HIRE, 6); overlay("LEVEL CLEARED", "Space to launch"); return;
            }
        }
    }
    if (ny == BPY && bdy > 0 && nx >= bpx && nx < bpx + PADW) {
        int off = nx - bpx;
        bdx = off <= 1 ? -2 : off <= 3 ? -1 : off == 4 ? (bdx > 0 ? 1 : -1) : off <= 6 ? 1 : 2;
        bdy = -1; ny = BPY - 1; tone(520, 20);
    }
    if (ny > BPY) {
        blives--; brk_head(); play(M_DIE, 3);
        if (blives <= 0) { bstate = G_OVER; overlay("GAME OVER", "Space to play again"); return; }
        ball_to_paddle(); bstate = G_READY; paddle_draw(); put(bbx, bby, 'O', A(CBG, CWHITE)); overlay("MISSED", "Space to launch"); return;
    }
    bbx = nx; bby = ny; put(bbx, bby, 'O', A(CBG, CWHITE));
    int iv = 70 - blevel * 6; bnext = g_now + (iv < 32 ? 32 : iv);
}
static void paddle_move(int d) {
    for (int i = 0; i < PADW; i++) put(bpx + i, BPY, ' ', NORM);
    bpx += d; if (bpx < BX0) bpx = BX0; if (bpx > BX1 - PADW + 1) bpx = BX1 - PADW + 1;
    paddle_draw();
    if (bstate == G_READY) { put(bbx, bby, ' ', NORM); ball_to_paddle(); put(bbx, bby, 'O', A(CBG, CWHITE)); }
}
static void brk_key(int k) {
    if (k == K_LEFT || k == 'a') { if (bstate != G_PAUSE && bstate != G_OVER) paddle_move(-3); return; }
    if (k == K_RIGHT || k == 'd') { if (bstate != G_PAUSE && bstate != G_OVER) paddle_move(3); return; }
    if (k == ' ' || k == K_ENTER) {
        if (bstate == G_OVER) { brk_reset(); brk_draw(); }
        if (bstate == G_READY) { bdx = (rnd() & 1) ? 1 : -1; bdy = -1; bstate = G_RUN; brk_draw(); bnext = g_now + 60; }
        else if (bstate == G_PAUSE) { bstate = G_RUN; brk_draw(); bnext = g_now + 60; }
        else if (bstate == G_RUN && k == ' ') { bstate = G_PAUSE; overlay("PAUSED", "Space to resume"); }
    } else if (k == 'p' && bstate == G_RUN) { bstate = G_PAUSE; overlay("PAUSED", "Space to resume"); }
}

/* ========================================================================
 *  PANEL DISPATCH, MENU, MAIN LOOP
 * ====================================================================== */
static void panel_draw(void) {
    switch (sel) {
        case 0: panel_head(); page_about(); break;
        case 1: panel_head(); page_experience(); break;
        case 2: panel_head(); page_skills(); break;
        case 3: panel_head(); page_education(); break;
        case 4: panel_head(); page_projects(); break;
        case 5: panel_head(); page_contact(); break;
        case 6: term_enter(); term_draw(); break;
        case 7: ask_draw(); break;
        case 8: sys_draw(); break;
        case 9: snake_draw(); break;
        default: brk_draw(); break;
    }
}
static void redraw_all(void) { draw_topbar(); draw_static_frames(); draw_sidebar(); panel_draw(); draw_bottom(); }
static void select_item(int i) {
    if (i < 0) i = NITEMS - 1;
    if (i >= NITEMS) i = 0;
    if (sel == 9 && sstate == G_RUN) sstate = G_PAUSE;
    if (sel == 10 && bstate == G_RUN) bstate = G_PAUSE;
    sel = i; page = 0; focus = F_MENU;
    draw_sidebar(); panel_draw(); draw_bottom(); tone(520, 12);
}
static void leave_app(void) {
    if (sel == 9 && sstate == G_RUN) { sstate = G_PAUSE; overlay("PAUSED", "Enter to resume"); }
    if (sel == 10 && bstate == G_RUN) { bstate = G_PAUSE; overlay("PAUSED", "Space to resume"); }
    focus = F_MENU; draw_sidebar(); draw_bottom();
}
static void handle_key(int k) {
    if (focus == F_MENU) {
        if (k == K_UP) select_item(sel - 1);
        else if (k == K_DOWN || k == K_TAB) select_item(sel + 1);
        else if (k == K_LEFT || k == K_RIGHT) {
            if (NPAGES[sel] > 1) {
                int np = page + (k == K_RIGHT ? 1 : -1);
                if (np >= 0 && np < NPAGES[sel]) { page = np; panel_draw(); tone(660, 10); }
            } else if (k == K_RIGHT && sel >= 6) { focus = F_APP; draw_sidebar(); draw_bottom(); }
        } else if (k == K_ENTER && sel >= 6) { focus = F_APP; draw_sidebar(); draw_bottom(); }
        else if (k >= '1' && k <= '9') select_item(k - '1');
        else if (k == '0') select_item(9);
        else if (k == '-') select_item(10);
        else if (k == 'k') select_item(sel - 1);
        else if (k == 'j') select_item(sel + 1);
        return;
    }
    if (k == K_ESC) { leave_app(); return; }
    if (k == K_TAB && sel != 6 && sel != 7) { select_item(sel + 1); return; }
    switch (sel) {
        case 6: term_key(k); break;
        case 7: ask_key(k); break;
        case 8: sys_draw(); break;
        case 9: snake_key(k); break;
        case 10: brk_key(k); break;
    }
}
static void tick(u32 now) {
    if (now >= clock_next) { clock_next = now + 1000; draw_clock(); }
    if (sel == 8 && now >= sys_next) { sys_next = now + 250; sys_live(); }
    if (focus == F_APP) {
        u32 ph = (now / 500) & 1;
        if ((sel == 6 || sel == 7) && ph != blink_phase) { blink_phase = ph; if (sel == 6) term_cursor(ph); else ask_cursor(ph); }
        if (sel == 9 && sstate == G_RUN && now >= snext) snake_step();
        if (sel == 10 && bstate == G_RUN && now >= bnext) brk_step();
    }
}

/* ---------- boot splash ---------- */
static int skip_splash;
static void wait_ms(u32 ms) {
    u32 end = millis() + ms;
    while (g_now < end) { millis(); sound_tick(g_now); if (kb_poll()) skip_splash = 1; if (skip_splash) break; }
}
static void splash(void) {
    fill(0, 0, 80, 25, ' ', NORM);
    draw_topbar();
    static const char *LY[5] = { "X...X", ".X.X.", "..X..", "..X..", "..X.." };
    static const char *LD[5] = { "XXXX.", "X...X", "X...X", "X...X", "XXXX." };
    for (int r = 0; r < 5; r++) for (int c = 0; c < 5; c++) {
        if (LY[r][c] == 'X') { put(28 + c * 2, 3 + r, 0xDB, A(CBG, CWHITE)); put(29 + c * 2, 3 + r, 0xDB, A(CBG, CWHITE)); }
        if (LD[r][c] == 'X') { put(42 + c * 2, 3 + r, 0xDB, A(CBG, CYEL));   put(43 + c * 2, 3 + r, 0xDB, A(CBG, CYEL)); }
    }
    text(34, 9, "YASH  DATIR", A(CBG, CWHITE));
    text(16, 10, "Senior Software Engineer  -  Frontend & Full Stack", A(CBG, CCYAN));
    static const char *LOG[6] = { "Entering 32-bit protected mode", "Kernel loaded from sector 1", "Loading blueprint palette (VGA DAC)",
                                  "PS/2 keyboard controller", "PIT timer, 1.193182 MHz", "CMOS real-time clock" };
    frame(14, 20, 52, 3, A(CBG, CLINE));
    play(M_CHIME, 3);
    for (int i = 0; i < 6; i++) {
        text(20, 13 + i, "[", A(CBG, CLINE)); text(21, 13 + i, " OK ", A(CBG, CMINT)); text(25, 13 + i, "]", A(CBG, CLINE));
        text(27, 13 + i, LOG[i], NORM);
        int w = (i + 1) * 50 / 6; fill(15, 21, w, 1, 0xDB, A(CBG, CCYAN));
        wait_ms(150);
        if (skip_splash) break;
    }
    wait_ms(350);
}

void kmain(void) {
    for (char *p = __bss_start; p < __bss_end; p++) *p = 0;
    vga_init(); pit_init(); millis();
    { int h, m, s, d, mo, y; rtc_get(&h, &m, &s, &d, &mo, &y); rs ^= (u32)(s + m * 60 + h * 3600) * 2654435761u; if (!rs) rs = 1; }
    snake_reset(); brk_reset();
    splash();
    sel = 0; page = 0; focus = F_MENU;
    redraw_all();
    for (;;) {
        u32 now = millis();
        sound_tick(now);
        int k = kb_poll();
        if (k) handle_key(k);
        tick(now);
    }
}
