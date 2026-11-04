/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca user interface. Four columns map to KNOB 1..4. Rendering is lazy:
 * every element remembers what it last drew and is redrawn only on change. */
#ifndef UI_SMALL
#define UI_SMALL                                   /* (gfx.c: minsize on the device; a test alone: nothing) */
#endif
#ifndef FELUCCA_VERSION
#define FELUCCA_VERSION "CLIPPER 1.3.1"  /* the synthesizer firmware for the FM-1 (from SLOOP 2.5, based on Felucca) */
#endif
static void project_save(uint32_t slot);
static void arrangement_save(void);
static void panel_setup(void);
static void project_load(uint32_t slot);
static int project_used(uint32_t slot);
static int up_used(uint32_t k);              /* user presets: upreset.c */
static int up_load(uint32_t k);
static int up_load_to(track_t *t, uint32_t k);
static uint32_t up_count(void);
static uint32_t up_nth(uint32_t n);
static uint32_t up_rank(uint32_t slot);
static void up_name(uint32_t k, char *b);
static void up_slot_label(char *b, uint32_t k);
static void up_ui(uint32_t op, uint32_t k);
UI_SMALL static uint32_t user_of(const track_t *t)    /* user preset slot its sound came from, UP_SLOTS = none */
{
    return t->user && up_used(t->user - 1u) ? t->user - 1u : UP_SLOTS;
}
static uint32_t up_gen;                      /* bumped on every user bank change (redraws) */
static uint8_t sync_reload;                  /* engine / preset / project / user preset loaded: editor RELOAD push */

#define ACC C_HI                   /* amber everywhere; white is the only accent */
#define VAL(c) ((c) == ui.hot_col && ui.hot_t ? C_WHITE : TE_COL[(c) & 3u])   /* LIVE: one colour per knob */
#define RATIO(d, v) ((d)->max > (d)->min ? ((int32_t)(v) - (d)->min) * 1000 / ((d)->max - (d)->min) : -1)
/* layout: four 60 px columns, 4 px inset */
/* Terminus 8x16 (S) and 16x32 (L) */
#define Y_HEAD 0
#define H_HEAD 20
#define Y_LABEL 26
#define Y_VALUE 44
#define Y_GAUGE 64
#define Y_SEP_END 70
#define Y_GRAPH 74
#define H_GRAPH 124
#define G_OY 24                       /* graphs draw in the lower 100 px; the focus readout sits on top */
#define Y_FOOT 202
#define H_FOOT 38

static struct {
    uint8_t home;
    uint8_t page;                /* index into PAGES */
    uint8_t fam_last[FAM_COUNT]; /* last page used per family */
    uint8_t bank;                /* SEQ: 16-step bank (follows the cursor) */
    uint8_t cursor;              /* SEQ: step being edited (STEP page KNOB 1 moves it) */
    uint8_t entry_open;          /* SEQ: keys held since the first press of this entry */
    uint8_t note_k;              /* (1.0, SLOOP 2.7.2) NOTES: the note of the cursor's chord (0..n-1) */
    uint8_t zoom;                /* (1.0) NOTES: the steps shown, NV_ZOOM[1 + zoom] (PRESETS) */
    uint8_t sheet;               /* (1.0) NOTES: the note's sheet (OCT+ held) open: 1 + its row; 0 closed */
    uint8_t hot_col, hot_t;      /* column whose knob was just turned (drawn white) */
    uint8_t menu;                /* 0 off, 1 list, 2 about (HOME held) */
    uint8_t menu_sel;
    uint32_t menu_sig, home_t0;  /* HOME press time (btn_hold) */
    uint8_t force;               /* full redraw pending */
    uint8_t msg_t;               /* transient message frames */
    uint8_t bpm_t;               /* frames the BPM stays highlighted after a SELECT turn */
    uint8_t arm, arm_t;          /* destructive action armed: param id, frames left to confirm */
    uint32_t rec_t0;             /* REC press time (btn_hold) */
    uint8_t confirm;             /* 1 = "clear the sequence?" (REC held on SEQ / ARP), 2 = "clear track n?" (TRACKS) */
    uint8_t confirm_trk;         /* the track the dialog clears */
    uint8_t uslot;               /* SAVE > USER: the selected user preset slot */
    /* layers (ui_layers.c): a function button held, the keys and knobs do something else */
    uint8_t layer;               /* the layer drawn (LY_*), LY_PLAY = none */
    uint8_t layer_btn;           /* its button (B_*), NB = none */
    uint8_t layer_used;          /* a key / knob / OCT was used while it was held: no tap on release */
    uint32_t layer_t0;           /* its press time (ms) */
    uint8_t step_page;           /* SEQ layer: the 16 steps shown (page x 16) */
    uint16_t step_held;          /* SEQ layer: the step keys held (white key index) */
    uint32_t step_sess;          /* SEQ layer: the undo session of this hold */
    uint8_t lock_par;            /* SEQ layer, a step held + KNOB 4: the parameter it locks (the last sound
                                  * parameter a knob changed on a page of the selected track; P_ED_FLT at boot) */
    uint8_t hold_kind;           /* a hold to confirm: 1 = clear the track (REC), 2 = save (SAVE) */
    uint32_t hold_t0;            /* (ms) */
    uint8_t hold_trk;
    uint32_t tap_ms[4];          /* tap tempo: the last taps */
    uint8_t tap_n;
    char msg[24];
    uint32_t enc_t[NE];
    /* drawn-state cache */
    char col[4][32];
    char focus_l[8], focus_v[8], focus_u[8];   /* the touched column, shown large */
    char big_l[4][8], big_v[4][10], big_u[4][8];   /* the four columns, for the big values (2.4: pages without a graph) */
    uint16_t big_c[4];
    uint32_t graph_sig, head_sig, foot_sig, frame;
    uint8_t graph_top;           /* the graph strip's top G_OY rows hold something */
} ui;

UI_SMALL static const page_t *cur_page(void) { return &PAGES[ui.page]; }
static int32_t accel(uint32_t role, int32_t s, int32_t range);   /* ui_input.c */
static void layer_screen_draw(void);                            /* ui_layers.c */
static void hold_screen_draw(void);
static uint8_t layer_shown;

UI_SMALL static uint32_t page_first(uint32_t fam)
{
    uint32_t i;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].fam == fam && !page_hidden(i))
            return i;
    return 0;
}

/* transient message in the top bar: a + b */
UI_SMALL static void ui_say(const char *a, const char *b)
{
    uint32_t n;
    str_cpy(ui.msg, a, sizeof ui.msg);
    n = str_len(ui.msg);
    str_cpy(ui.msg + n, b, sizeof ui.msg - n);
    ui.msg_t = 40;
}

UI_SMALL static void ui_message(const char *s) { ui_say(s, ""); }

UI_SMALL static void page_entered(void)
{
    const page_t *pg = cur_page();
    song.seq_mode = !ui.home && pg->fam == FAM_SEQ;
    ui.entry_open = 0;
    ui.hot_t = 0;                                /* the white value / focus box was the old page's */
    ui.force = 1;
}

UI_SMALL static int step_on(const step_t *st) { return st->time == ST_NOTE && st->n; }
/* (1.0, SLOOP 2.6) a note's length: its step and the TIEs after it (inside LEN); note_set_len makes it want steps
 * long: shorter, its last TIEs become RESTs; longer, the RESTs after it become TIEs, up to the next step with notes
 * (or LEN). Returns its length now */
UI_SMALL static uint32_t note_len(const track_t *t, uint32_t i)
{
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), n = 1;
    while (i + n < len && t->step[i + n].time == ST_TIE)
        n++;
    return n;
}
UI_SMALL static uint32_t note_set_len(track_t *t, uint32_t i, uint32_t want)
{
    uint32_t len = (uint32_t)clamp(t->p[P_SLEN], 1, NSTEP), n = note_len(t, i);
    for (; n > want && n > 1u; n--) {
        if (!t->step[i + n - 1u].n)                    /* (a TIE holding notes, from an older pattern, keeps them) */
            memset(&t->step[i + n - 1u], 0, sizeof(step_t));
        t->step[i + n - 1u].time = ST_REST;
    }
    for (; n < want && i + n < len && !t->step[i + n].n; n++) {
        memset(&t->step[i + n], 0, sizeof(step_t));
        t->step[i + n].time = ST_TIE;
    }
    return n;
}
/* ---- (SLOOP 2.8.3) LEN under later notes ----
 * POLY: a note's LEN runs on under the notes after it: it joins their steps as a note going on (SF_CONT, as a
 * recording over held notes writes it, seq.c rec_hold), 4 notes a step at most; after them, a NOTE of its notes only,
 * all going on, carries it (the others end there). A long bass under a melody, programmed. MONO / LEGATO parts play one
 * note at a time: it stops there and says so. The step format is 2.7's: no project change.
 * The work: seq.c span_len / span_set (a free take uses them too); span_why: why it stopped short */
UI_SMALL static uint32_t note_span(const track_t *t, uint32_t i)   /* LEN: the steps step i's notes sound */
{
    uint8_t s[4];
    uint32_t ns;
    if (!step_on(&t->step[i]) || !(ns = span_notes(&t->step[i], s)))
        return note_len(t, i);
    return span_len(t, i, s, ns);
}
UI_SMALL static uint32_t note_span_set(track_t *t, uint32_t i, uint32_t want)
{
    uint8_t s[4];
    uint32_t ns;
    span_why = 3;
    if (!step_on(&t->step[i]) || !(ns = span_notes(&t->step[i], s)))
        return note_set_len(t, i, want);
    return span_set(t, i, s, ns, want);
}

/* a synth step's velocity as it plays (seq.c step_vel: ACC 127, else its VEL, 96 when 0, through its first note's level) */
UI_SMALL static uint32_t step_vel_shown(const step_t *st)
{
    uint32_t base = (st->flags & SF_ACCENT) ? 127u : (st->vel ? st->vel : 96u);
    return lvl_vel(st->lvl & 3u, base);
}
/* step i of track t has something to play (synth: notes, drums: a lane) */
UI_SMALL static int trk_step_on(const track_t *t, uint32_t i)
{
    return is_drum(t) ? dstep_mask(&t->dstep[i % NSTEP]) != 0u : step_on(&t->step[i % NSTEP]);
}

UI_SMALL static void step_clear(step_t *st)
{
    memset(st, 0, sizeof *st);
    st->time = ST_REST;
}

/* undo / redo (EDIT + OCT- / OCT+): the marked pattern and the one now swap places */
UI_SMALL static int undo_swap(int redo)
{
    track_t *t;
    int16_t len;
    if (!undo.valid || (uint32_t)!!redo != undo.undone)
        return 0;
    t = &trk[undo.trk % NTRK];
    fm1_irq_off();
    {
        uint32_t i;
        for (i = 0; i < NSTEP; i++) {
            step_t x = t->step[i];
            int8_t m = t->micro[i];
            t->step[i] = undo.st[i];
            undo.st[i] = x;
            t->micro[i] = undo.micro[i];                 /* (1.0, as SLOOP 2.7: and its nudge; undo_mark kept it) */
            undo.micro[i] = m;
        }
    }
    len = t->p[P_SLEN];
    t->p[P_SLEN] = undo.len;
    undo.len = len;
    undo.undone = (uint8_t)!redo;
    fm1_irq_on();
    sync_reload = 1;
    ui.force = 1;
    return 1;
}

/* SEQ cursor: wraps inside the pattern length, the bank follows, a step entry ends */
UI_SMALL static void cursor_set(int32_t c)
{
    int32_t len = TSEL->p[P_SLEN] > 0 ? TSEL->p[P_SLEN] : 1;
    ui.cursor = (uint8_t)((c % len + len) % len);
    ui.bank = (uint8_t)(ui.cursor / 16u);
    ui.entry_open = 0;
    ui.note_k = 0;
}

UI_SMALL static void cursor_fix(void)                           /* LEN got shorter: onto the last step */
{
    if (ui.cursor >= (uint32_t)TSEL->p[P_SLEN])
        cursor_set(TSEL->p[P_SLEN] - 1);
    if (ui.note_k && ui.note_k >= TSEL->step[ui.cursor].n)
        ui.note_k = (uint8_t)(TSEL->step[ui.cursor].n ? TSEL->step[ui.cursor].n - 1u : 0u);
}

/* ---- 1.0, after SLOOP 2.7.2: SEQ > NOTES, the note editor (after Melodee's NOTES page, keremimo, GPL-3.0: the idea,
 * not its code). The note under edit: step ui.cursor, note ui.note_k of its chord. KNOB 1 walks the notes (a chord's one
 * by one) and the empty steps; a note's TIEs are its length, not stops. KNOB 2 its pitch, KNOB 3 its length
 * (note_set_len), KNOB 4 its FORCE, PRESETS the zoom; OCT+ held its sheet (SLIDE, NUDGE, FILL, MOVE, COPY, DELETE) */
static const uint8_t NV_ZOOM[4] = {8, 16, 32, 64};
static const char *const FORCE_N[5] = {"GHOST", "SOFT", "NORM", "HARD", "ACC"};
static const uint8_t F_LVL[4] = {LV_GHOST, LV_SOFT, LV_NORM, LV_HARD};
static const char *const SHEET_N[6] = {"SLIDE", "NUDGE", "FILL", "MOVE", "COPY", "DELETE"};
static const char *const FILL_N[3] = {"NORMAL", "FILL", "NO FILL"};
UI_SMALL static void note_walk(int32_t dir)
{
    track_t *t = TSEL;
    uint32_t len = trk_len(t), i = ui.cursor % len, guard = len;
    const step_t *st = &t->step[i];
    if (dir > 0 && step_on(st) && ui.note_k + 1u < st->n) {
        ui.note_k++;
        return;
    }
    if (dir < 0 && step_on(st) && ui.note_k) {
        ui.note_k--;
        return;
    }
    do                                                  /* (the TIEs belong to the note before them) */
        i = dir > 0 ? (i + 1u) % len : (i + len - 1u) % len;
    while (--guard && t->step[i].time == ST_TIE);
    cursor_set((int32_t)i);
    if (dir < 0 && step_on(&t->step[i]))
        ui.note_k = (uint8_t)(t->step[i].n - 1u);       /* (back: the chord's last note) */
}
UI_SMALL static uint32_t note_force(const step_t *st, uint32_t k)   /* 0 GHOST .. 3 HARD, 4 ACC (the step's accent) */
{
    uint32_t l = (st->lvl >> (2u * k)) & 3u;
    if (st->flags & SF_ACCENT)
        return 4u;
    return l == LV_GHOST ? 0u : l == LV_SOFT ? 1u : l == LV_HARD ? 3u : 2u;
}
UI_SMALL static void note_force_set(step_t *st, uint32_t k, uint32_t f)
{
    uint32_t sh = 2u * k;
    st->lvl = (uint8_t)((st->lvl & ~(3u << sh)) | (uint32_t)F_LVL[f > 3u ? 3u : f] << sh);
    st->flags = (uint8_t)((st->flags & ~SF_ACCENT) | (f == 4u ? SF_ACCENT : 0u));
}
UI_SMALL static uint8_t drop2(uint32_t v, uint32_t k)            /* a 2-bit field per note: note k's taken out */
{
    return (uint8_t)((v & ((1u << (2u * k)) - 1u)) | ((v >> (2u * k + 2u)) << (2u * k)));
}
UI_SMALL static void note_del_one(step_t *st, uint32_t k)   /* note k out of a chord's step */
{
    uint32_t j, c;
    for (j = k; j + 1u < st->n; j++)
        st->note[j] = st->note[j + 1u];
    st->lvl = drop2(st->lvl, k);
    st->rat = drop2(st->rat, k);
    c = (st->flags & SF_CONT_ALL) / SF_CONT0;
    c = (c & ((1u << k) - 1u)) | ((c >> (k + 1u)) << k);
    st->flags = (uint8_t)((st->flags & ~SF_CONT_ALL) | c * SF_CONT0);
    st->n--;
}
UI_SMALL static void note_del(track_t *t, uint32_t i, uint32_t k)   /* one note of a chord; the last one: the step */
{
    step_t *st = &t->step[i];
    uint32_t j, c;
    if (!step_on(st))
        return;
    if (st->n <= 1u) {
        note_span_set(t, i, 1);                         /* (its TIEs: RESTs; SLOOP 2.8.3: where it went on under others, out) */
        step_clear(st);
        return;
    }
    {   /* (SLOOP 2.8.3) one note of a chord: the chord back to one step, then the others as long again (its going on: gone) */
        uint32_t w = note_span(t, i);
        if (w > 1u && w != note_len(t, i)) {
            note_span_set(t, i, 1);
            note_del_one(st, k);
            note_span_set(t, i, w);
            return;
        }
    }
    for (j = k; j + 1u < st->n; j++)
        st->note[j] = st->note[j + 1u];
    st->lvl = drop2(st->lvl, k);
    st->rat = drop2(st->rat, k);
    c = (st->flags & SF_CONT_ALL) / SF_CONT0;
    c = (c & ((1u << k) - 1u)) | ((c >> (k + 1u)) << k);
    st->flags = (uint8_t)((st->flags & ~SF_CONT_ALL) | c * SF_CONT0);
    st->n--;
}
UI_SMALL static int step_free(const track_t *t, uint32_t i)     /* nothing there: no note, no TIE */
{
    return !step_on(&t->step[i]) && t->step[i].time != ST_TIE;
}
/* a note (its step, TIEs, nudge, fill, locks) one step on or back (copy: to just after it, the original stays);
 * the steps it goes to must be free, inside the pattern. Returns where it is now, or -1 */
UI_SMALL static int32_t note_shift(track_t *t, uint32_t i, int32_t dir, int copy)
{
    uint32_t len = trk_len(t), n = note_len(t, i), j, to;
    if (!step_on(&t->step[i]) || note_span(t, i) != n)
        return -1;                                      /* (SLOOP 2.8.3: one going on under later notes: LEN 1 first) */
    to = copy ? i + n : dir > 0 ? i + 1u : i - 1u;
    if ((!copy && dir < 0 && i == 0u) || to + n > len)
        return -1;
    for (j = 0; j < n; j++)
        if ((to + j < i || to + j >= i + n) && !step_free(t, to + j))
            return -1;
    fm1_irq_off();
    if (copy) {
        for (j = 0; j < n; j++)
            t->step[to + j] = t->step[i + j];
    } else if (dir > 0) {
        for (j = n; j > 0; j--)
            t->step[i + j] = t->step[i + j - 1u];
        step_clear(&t->step[i]);
    } else {
        for (j = 0; j < n; j++)
            t->step[i - 1u + j] = t->step[i + j];
        step_clear(&t->step[i + n - 1u]);
    }
    t->micro[to] = t->micro[i];
    step_fill_set(t, to, step_fill(t, i));
    for (j = 0; j < NLOCK; j++)
        if (t->lock[j].step == i) {
            if (!copy)
                t->lock[j].step = (uint8_t)to;
            else {
                int k = lock_find(t, to, t->lock[j].param, 1);
                if (k >= 0) {
                    t->lock[k].step = (uint8_t)to;
                    t->lock[k].param = t->lock[j].param;
                    t->lock[k].val = t->lock[j].val;
                }
            }
        }
    if (!copy) {
        t->micro[i] = 0;
        step_fill_set(t, i, FC_NORM);
    }
    fm1_irq_on();
    return (int32_t)to;
}
/* OCT+ on the NOTES page: a tap acts on release (the octave, as everywhere), a hold (0.4 s) opens the sheet. Returns
 * pressed without OCT+ while it is down; *ev: 1 tapped, 2 held. OCT- pressed meanwhile: OCT+ as before */
static uint8_t octp;
static uint32_t octp_t;
UI_SMALL static uint32_t octp_filter(uint32_t pressed, uint32_t *ev)
{
    uint32_t ub = 1u << panel.btn[B_OCTUP];
    *ev = 0;
    if (pressed & ub) {
        octp = 1;
        octp_t = fm1_ms;
        pressed &= ~ub;
    }
    if (octp == 1u && (fm1_in.buttons & (1u << panel.btn[B_OCTDN]))) {
        octp = 0;
        return pressed | ub;                            /* (both: the octave back to 0, as before) */
    }
    if (octp && !(fm1_in.buttons & ub)) {
        *ev = octp == 1u;
        octp = 0;
    } else if (octp == 1u && fm1_ms - octp_t >= 400u) {
        octp = 2;
        *ev = 2;
    }
    return pressed;
}
/* the sheet over the NOTES page: six rows, the one KNOB 1 picks lit, values on the right */
UI_SMALL static void sheet_draw(int32_t x, int32_t y, const char *const *lab, const char *const *val, uint32_t sel, uint16_t acc)
{
    int32_t w = 124, k;
    cv_rect(x - 1, y - 1, w + 2, 6 * 16 + 4, C_LINE);
    cv_rect(x, y, w, 6 * 16 + 2, C_BLACK);
    for (k = 0; k < 6; k++) {
        int32_t ry = y + 2 + k * 16;
        if ((uint32_t)k == sel)
            cv_rect(x + 1, ry - 1, w - 2, 15, acc);
        cv_text(x + 5, ry, &FONT_S, lab[k], (uint32_t)k == sel ? C_WHITE : C_HI);
        if (val[k] && val[k][0])
            cv_text(x + w - 5 - text_w(&FONT_S, val[k]), ry, &FONT_S, val[k], (uint32_t)k == sel ? C_WHITE : C_AMB);
    }
}

UI_SMALL static void note_name(char *b, uint32_t n)
{
    str_cpy(b, N_NOTE[n % 12u], 4);
    fmt_int(b + str_len(b), (int32_t)(n / 12u) - 1);
}

UI_SMALL static void open_family(uint32_t fam)
{
    if (!ui.home && cur_page()->fam == fam) {          /* same button again: next page */
        uint32_t i = ui.page + 1u;
        while (i < NPAGES && PAGES[i].fam == fam && page_hidden(i))   /* (0.6: FMX's pages on FMX only) */
            i++;
        if (i >= NPAGES || PAGES[i].fam != fam)
            i = page_first(fam);
        ui.page = (uint8_t)i;
    } else {
        ui.page = ui.fam_last[fam] && PAGES[ui.fam_last[fam]].fam == fam && !page_hidden(ui.fam_last[fam]) ?
                  ui.fam_last[fam] : (uint8_t)page_first(fam);
    }
    ui.fam_last[fam] = ui.page;
    ui.home = 0;
    page_entered();
}

/* SELECT on a page: the previous / next page of its family, as tapping the button again but both ways,
 * stopping at the ends. Not the screens of their own (SONG, DRUMS) nor a family of one page (TRACKS):
 * 0 then, and SELECT is the tempo there */
UI_SMALL static int page_walk(int32_t s)
{
    const page_t *pg = cur_page();
    uint32_t i, fam = pg->fam, n = 0;
    int32_t cur = -1, to;
    uint8_t idx[16];
    if (ui.home || pg->scope == SC_SONG || pg->scope == SC_DRUM)
        return 0;
    for (i = 0; i < NPAGES && n < 16u; i++) {
        if (PAGES[i].fam != fam || PAGES[i].scope == SC_SONG || PAGES[i].scope == SC_DRUM || page_hidden(i))
            continue;
        if (i == ui.page)
            cur = (int32_t)n;
        idx[n++] = (uint8_t)i;
    }
    if (n < 2u || cur < 0)
        return 0;
    to = clamp(cur + s, 0, (int32_t)n - 1);
    if (to != cur) {
        ui.page = idx[to];
        ui.fam_last[fam] = ui.page;
        page_entered();
    }
    return 1;
}

UI_SMALL static void go_home(void)
{
#if FELUCCA_ARRANGER && !CLIPPER_SYNTH
    ui.home = 0;
    ui.page = (uint8_t)page_first(FAM_TRK);
#else
    ui.home = 1;
#endif
    ui.entry_open = 0;
    ui.hot_t = 0;
    song.seq_mode = 0;
    ui.force = 1;
}

/* ------------------------------------------------------- track setup --- */
/* LIVE: no factory sequence patterns. Loading a sound (factory or user preset) never
 * writes the sequencer: every pattern is the one the player records or enters. */

/* the parts' sounds at power-on (engine, preset): bass, pad, lead */
#if CLIPPER_SYNTH                  /* Clipper: the instrument plays CLIPPER (HYBRID's first patch) */
static const uint8_t TRK_DEF[NPART][2] = {{ENGI_HYBRID, 0}, {1, 0}, {4, 5}};
#else
static const uint8_t TRK_DEF[NPART][2] = {{0, 0}, {1, 0}, {4, 5}};   /* ANALOG 808 BOOM, DIGITAL RHODES, SAMPLE LOFI FLUTE */
#endif
UI_SMALL static uint32_t trk_def_engine(uint32_t i) { return i < NPART ? TRK_DEF[i][0] : 0u; }

UI_SMALL static int seq_is_empty(const track_t *t) { return track_empty(t); }

UI_SMALL static void track_defaults_steps(track_t *t) { steps_clear(t); }

/* what loading a sound (factory or user preset) leaves alone: the mix (LEVEL, PAN, MUTE:
 * the TRACKS faders, and the track FILTER), the pattern parameters (LEN, DIV, SWING, GATE) and the key the part plays
 * in (ROOT, SCALE, QNT, CHORD: the song's; SEL + key sets the root of every part). The SLICER is
 * part of the sound: a factory preset turns it OFF (its defaults), a user preset brings its own */
UI_SMALL static int param_kept(uint32_t i)
{
    return i == P_LEVEL || i == P_PAN || i == P_MUTE || (i >= P_SLEN && i <= P_SGATE) ||
           (i >= P_ROOT && i <= P_QUANT) || i == P_CHORD || i == P_TFLT || i == P_STRUM || i == P_VLEAD;
}

/* preset pi of the engine the track asked for: the whole sound (not the pattern parameters) */
/* 0.7 (after SLOOP 2.7): a sound loaded while the arpeggiator runs keeps it: its settings (MODE .. ORDER) and the notes
 * it holds (seq.c arp_keep_req: the panic of this track leaves them). KBD HOLD (HOLD with the arp OFF) stays on. With
 * the arp OFF, a preset brings its own arp, as before */
UI_SMALL static int arp_param(uint32_t i) { return i >= P_AMODE && i <= P_AORDER; }
#if CLIPPER_SYNTH
static void hist_before_load(void);                  /* (0.7, ui_synth.c: the edits a step of their own, first) */
#endif
UI_SMALL static void apply_preset_to(track_t *t, uint32_t pi)
{
    const engine_t *e = ENGINES[t->eng_req % NENGINES];
    uint32_t i;
    int keep_arp = arp_user_on(t), kbd_hold = !t->p[P_AMODE] && t->p[P_AHOLD];   /* (1.0: the player's arp only) */
    if (is_drum(t))
        return;
    if (keep_arp)
        arp_keep_req |= (uint8_t)(1u << trk_index(t));   /* (before the panic below) */
#if CLIPPER_SYNTH
    if (t == &trk[0])
        hist_before_load();
#endif
    sound_fade_begin(t);                              /* Clipper: what sounds fades out with its own values */
    panic_req |= (uint8_t)(1u << trk_index(t));       /* MONO/POLY may change: release what sounds */
    t->user = 0;
    if (t == TSEL)
        sync_reload = 1;
    if (!e->npresets)
        return;
    pi %= e->npresets;
    t->preset = (uint8_t)pi;
    for (i = 0; i < P_E0; i++)                        /* the rest of the sound to its defaults: a preset */
        if (!param_kept(i) && !(keep_arp && arp_param(i)) && !(kbd_hold && i == P_AHOLD))
            t->p[i] = TP[i].def;                     /* sounds the same after any edit (not the pattern, not the mix) */
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = (int16_t)e->presets[pi].e[i];
    t->p[P_ATK] = e->presets[pi].env[0];
    t->p[P_DEC] = e->presets[pi].env[1];
    t->p[P_SUS] = e->presets[pi].env[2];
    t->p[P_REL] = e->presets[pi].env[3];
    t->p[P_ED_FLT] = e->presets[pi].fenv;
    t->p[P_ED_FX] = preset_trim(t->eng_req % NENGINES, pi);  /* level-matched (tools/level_presets.py) */
    t->p[P_VOICE] = e->presets[pi].mono ? V_LEGATO : V_POLY;   /* mono presets keep the legato feel */
    {   /* the rest of the patch: sends, arpeggiator (never a pattern: LIVE) */
        static const uint8_t FX_DEF[4] = {0, 24, 28, 36};
        const preset_t *pr = &e->presets[pi];
        int16_t ka[P_AORDER - P_AMODE + 1];
        for (i = 0; i < NELEM(ka); i++)
            ka[i] = t->p[P_AMODE + i];
        for (i = 0; i < 4u; i++) {
            t->p[P_DIST + i] = (int16_t)(pr->fx[i] ? pr->fx[i] - 1 : FX_DEF[i]);
            if (CLIPPER_SYNTH && (P_DIST + i == P_DLY || P_DIST + i == P_REV))
                t->p[P_DIST + i] = 0;                /* (1.0, asked by users: the factory sounds dry, no delay and no
                                                      * reverb until FX > DLY / REV or the SPACE macro adds them) */
            t->p[P_AMODE + i] = (int16_t)(pr->arp[i] ? pr->arp[i] - 1 : TP[P_AMODE + i].def);
        }
        preset_extras(t->p, pr);                     /* glide, pitch / LFO modulation, voice mode */
        if (keep_arp)                                /* (0.7) the arp that runs goes on: its settings as they were */
            for (i = 0; i < NELEM(ka); i++)
                t->p[P_AMODE + i] = ka[i];
        else if (kbd_hold)
            t->p[P_AHOLD] = 1;                       /* (0.7) KBD HOLD stays on */
        arp_snd[trk_index(t) % NTRK] = (uint8_t)(keep_arp ? 0 : t->p[P_AMODE]);   /* (1.0) whose arp it is now */
    }
#if CLIPPER_SYNTH
    mseq_init(t);                                    /* (0.8) the MOD SEQ: the shapes to start from */
#endif
    fm6_track_loaded(t);                             /* FM6: the preset's patch (its PTCH) */
}

/* the engine's defaults and its first preset. With the audio IRQ off: the ISR sees the old engine with
 * its values or the new one with its own (voice.c engine_block), never one with the other's */
UI_SMALL static void set_engine_of(track_t *t, uint32_t ei)
{
    const engine_t *e = ENGINES[ei % NENGINES];
    uint32_t i;
    if (is_drum(t))
        return;
#if CLIPPER_SYNTH
    if (t == &trk[0])
        hist_before_load();
#endif
    sound_fade_begin(t);
    fm1_irq_off();
    locks_restore(t);                                 /* (a lock in force: its base first, then the new sound) */
    t->eng_req = (uint8_t)(ei % NENGINES);
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = e->edit[i].def;
    apply_preset_to(t, 0);
    fm1_irq_on();
}

UI_SMALL static void apply_preset(uint32_t pi) { apply_preset_to(TSEL, pi); }
UI_SMALL static void set_engine(uint32_t ei) { set_engine_of(TSEL, ei); }

UI_SMALL static void track_defaults(track_t *t)
{
    uint32_t i;
    for (i = 0; i < P_E0; i++)
        t->p[i] = TP[i].def;
    track_defaults_steps(t);
}

/* switch engine (its defaults + first preset) and say so */
UI_SMALL static void select_engine(uint32_t e)
{
    if (is_drum(TSEL))
        return;
    set_engine(e);
    ui_say("ENGINE ", ENGINES[TSEL->eng_req]->name);
    ui.force = 1;
}

/* the factory presets as one list by kind (basses, keys, organs, pads, leads, plucks and bells, stabs,
 * the rest), then the used user presets: the PRESETS knob and the PRESETS page browse it. By name: an
 * engine's preset table may change order; tests/ui_pages_test.c checks every preset is here once */
enum { BK_BASS, BK_KEYS, BK_ORGAN, BK_PAD, BK_LEAD, BK_PLUCK, BK_STAB, BK_FX };
static const char *const BANK_KIND[] = {"BASS", "KEYS", "ORGN", "PAD", "LEAD", "PLCK", "STAB", "FX"};
/* the sample-based factory sounds (SAMPLE 4, GRAIN 8): not in the Clipper synth, which has no built-in samples */
#if CLIPPER_SYNTH
#define BK_SMP(k, e, n)
#else
#define BK_SMP(k, e, n) {k, e, n},
#endif
static const struct { uint8_t kind, e; const char *name; } BANK[] = {
    /* MAIN MENU HOUSE: 20 chords (MIDI program 0-19), 20 leads (20-39), 20 basses (40-59) */
    {BK_ORGAN, ENGI_HYBRID, "ORGAN STB"}, {BK_ORGAN, 7, "HOUSE ORGN"}, {BK_ORGAN, 7, "FULL ORGAN"},
    {BK_ORGAN, 7, "DIRTY B3"}, {BK_KEYS, 1, "M1 PIANO"}, {BK_KEYS, ENGI_HYBRID, "SOFT EP"}, {BK_KEYS, 1, "DX RHODES"},
    {BK_KEYS, ENGI_HYBRID, "WURLI HSE"}, {BK_KEYS, 1, "SUITCASE"}, {BK_STAB, ENGI_HYBRID, "DUB TECH"},
    {BK_STAB, ENGI_HYBRID, "JUNO STAB"}, {BK_STAB, ENGI_HYBRID, "DEEP STAB"}, {BK_STAB, 6, "MAJ STAB"},
    {BK_STAB, 6, "SUS STAB"}, {BK_STAB, 6, "RAVE STAB"}, {BK_STAB, ENGI_MACRO, "HOUSE CHRD"},
    {BK_STAB, ENGI_MACRO, "CHORD STAB"}, {BK_PAD, ENGI_JUNO, "JUNO 106 PAD"}, {BK_PAD, ENGI_STRINGS, "SOLINA"},
    {BK_PAD, ENGI_HYBRID, "WARM PAD"},
    {BK_LEAD, ENGI_HYBRID, "PAN PIPE"}, {BK_LEAD, ENGI_HYBRID, "SH LEAD"}, {BK_LEAD, 0, "PROPH LEAD"},
    {BK_LEAD, ENGI_HYBRID, "PWM SQR"}, {BK_LEAD, ENGI_HYBRID, "SINE LEAD"}, {BK_LEAD, 0, "FAT LEAD"},
    {BK_LEAD, ENGI_HYBRID, "MOOG LEAD"}, {BK_LEAD, ENGI_HYBRID, "SOFT SYNC"}, {BK_LEAD, ENGI_HYBRID, "SYNC LEAD"},
    {BK_LEAD, ENGI_JUNO, "SAW LEAD 106"}, {BK_LEAD, ENGI_JUNO, "SYNTHWAVE LD"}, {BK_LEAD, 1, "FM LEAD"},
    {BK_LEAD, 2, "CZ LEAD"}, {BK_LEAD, ENGI_ACID, "ACID LEAD"}, {BK_LEAD, 6, "SQR LEAD"}, {BK_LEAD, 5, "AAH LEAD"},
    {BK_PLUCK, ENGI_HYBRID, "VIBES"}, {BK_PLUCK, 1, "MARIMBA"}, {BK_PLUCK, 1, "KALIMBA"}, {BK_PLUCK, 0, "SQR PLUCK"},
    {BK_BASS, 0, "TAURUS"}, {BK_BASS, 0, "MONO BASS"}, {BK_BASS, ENGI_HYBRID, "ROLL BASS"},
    {BK_BASS, ENGI_HYBRID, "DEEP SUB"}, {BK_BASS, 0, "MINI BASS"}, {BK_BASS, ENGI_HYBRID, "HSE BASS"},
    {BK_BASS, 0, "DYN BASS"}, {BK_BASS, ENGI_HYBRID, "TX BASS"}, {BK_BASS, ENGI_HYBRID, "DUB BASS"},
    {BK_BASS, 6, "FAT BASS"}, {BK_BASS, 0, "ACID 303"}, {BK_BASS, ENGI_ACID, "DEEP 303"},
    {BK_BASS, ENGI_ACID, "SOFT 303"}, {BK_BASS, ENGI_ACID, "LOW 303"}, {BK_BASS, ENGI_ACID, "ACID HOUSE"},
    {BK_BASS, ENGI_JUNO, "SUB BASS 106"}, {BK_BASS, ENGI_JUNO, "JUNO BASS"}, {BK_BASS, ENGI_FM6, "ROUND BASS"},
    {BK_BASS, 0, "PLUGG BASS"}, {BK_BASS, 0, "FUNK BASS"},
};
#define NBANK (sizeof BANK / sizeof BANK[0])
static uint8_t bank_pi[NBANK];                       /* the preset index of each entry in its engine */
static uint8_t bank_ready;
UI_SMALL static void bank_resolve(void)
{
    uint32_t i, k;
    for (i = 0; i < NBANK; i++) {
        const engine_t *e = ENGINES[BANK[i].e % NENGINES];
        bank_pi[i] = 0xFF;
        for (k = 0; k < e->npresets; k++)
            if (str_eq(e->presets[k].name, BANK[i].name))
                bank_pi[i] = (uint8_t)k;
    }
    bank_ready = 1;
}
UI_SMALL static uint32_t base_pos(uint32_t *total)            /* list index of the selected part's sound (no favourites) */
{
    uint32_t i, cur = 0;
    if (!bank_ready)
        bank_resolve();
    for (i = 0; i < NBANK; i++)
        if (BANK[i].e == TSEL->eng_req && bank_pi[i] == TSEL->preset)
            cur = i;
    if (user_of(TSEL) < UP_SLOTS)
        cur = NBANK + up_rank(user_of(TSEL));
    *total = NBANK + up_count();
    return cur;
}

/* list index n (< total) -> engine, *k its preset; NENGINES = user preset, *k its slot (no favourites) */
UI_SMALL static uint32_t base_at(uint32_t n, uint32_t *k)
{
    if (!bank_ready)
        bank_resolve();
    if (n >= NBANK) {
        *k = up_nth(n - NBANK);
        return NENGINES;
    }
    *k = bank_pi[n] == 0xFF ? 0u : bank_pi[n];
    return BANK[n].e;
}
UI_SMALL static const char *base_kind(uint32_t n) { return n < NBANK ? BANK_KIND[BANK[n].kind] : "USER"; }

/* 0.7, favourites (after SLOOP 2.7; PRESETS page, KNOB 4): up to 16 sounds at the top of the PRESETS list, kept in the
 * FM-1's settings (project.c, the settings' side record). An entry: a factory sound, its BANK index; a user preset,
 * FAV_USER | its slot (an erased one is left out of the list). The layer's list and the editor keep the list without
 * them */
#define FAV_N 16u
#define FAV_NONE 0xFFFFu
#define FAV_USER 0x8000u
static uint16_t fav[FAV_N] = {FAV_NONE, FAV_NONE, FAV_NONE, FAV_NONE, FAV_NONE, FAV_NONE, FAV_NONE, FAV_NONE,
                              FAV_NONE, FAV_NONE, FAV_NONE, FAV_NONE, FAV_NONE, FAV_NONE, FAV_NONE, FAV_NONE};
static int8_t fav_at = -1;                           /* the favourite the selected part's sound was loaded from */
UI_SMALL static uint32_t fav_list(uint32_t *b)                /* the favourites as base list indices, in order */
{
    uint32_t i, n = 0;
    for (i = 0; i < FAV_N; i++) {
        uint32_t f = fav[i];
        if (f == FAV_NONE)
            continue;
        if (f & FAV_USER) {
            if (!up_used(f & 0xFFu))
                continue;
            b[n++] = NBANK + up_rank(f & 0xFFu);
        } else if (f < NBANK) {
            b[n++] = f;
        }
    }
    return n;
}
UI_SMALL static uint32_t fav_key(void)                        /* the selected part's sound as a favourite entry */
{
    uint32_t i;
    if (!bank_ready)
        bank_resolve();
    if (user_of(TSEL) < UP_SLOTS)
        return FAV_USER | user_of(TSEL);
    for (i = 0; i < NBANK; i++)
        if (BANK[i].e == TSEL->eng_req && bank_pi[i] == TSEL->preset)
            return i;
    return FAV_NONE;
}
UI_SMALL static int fav_has(uint32_t key)
{
    uint32_t i;
    for (i = 0; i < FAV_N; i++)
        if (key != FAV_NONE && fav[i] == key)
            return 1;
    return 0;
}
UI_SMALL static void fav_toggle(int add)                      /* KNOB 4 on the PRESETS page: right adds, left removes */
{
    uint32_t key = fav_key(), i;
    if (key == FAV_NONE || is_drum(TSEL))
        return;
    if (add) {
        if (fav_has(key)) {
            ui_message("FAV: ALREADY");
            return;
        }
        for (i = 0; i < FAV_N && fav[i] != FAV_NONE; i++)
            ;
        if (i == FAV_N) {
            ui_message("FAVS FULL (16)");
            return;
        }
        fav[i] = (uint16_t)key;
        ui_message("FAV ADDED");
    } else {
        for (i = 0; i < FAV_N && fav[i] != key; i++)
            ;
        if (i == FAV_N)
            return;
        for (; i + 1u < FAV_N; i++)                  /* (the order kept) */
            fav[i] = fav[i + 1u];
        fav[FAV_N - 1u] = FAV_NONE;
        fav_at = -1;
        ui_message("FAV REMOVED");
    }
    settings_later = 1;                              /* (project.c: the side record goes with the settings) */
    ui.force = 1;
}

/* the PRESETS list: the favourites, then every factory sound by kind, then the user presets */
UI_SMALL static uint32_t preset_pos(uint32_t *total)
{
    uint32_t b[FAV_N], nf = fav_list(b), cur = base_pos(total);
    *total += nf;
    if (fav_at >= 0 && (uint32_t)fav_at < nf && b[fav_at] == cur)
        return (uint32_t)fav_at;
    return nf + cur;
}
UI_SMALL static uint32_t preset_at(uint32_t n, uint32_t *k)
{
    uint32_t b[FAV_N], nf = fav_list(b);
    return base_at(n < nf ? b[n] : n - nf, k);
}
UI_SMALL static const char *preset_kind(uint32_t n)
{
    uint32_t b[FAV_N], nf = fav_list(b);
    return n < nf ? "FAV" : base_kind(n - nf);
}

/* MAIN MENU HOUSE: each sound's own space, in BANK order. The factory sounds come dry (the Clipper synth zeroes their
 * delay and reverb sends), a house sound wants its room: INSERT (0 none, 9 JUNO I, 10 JUNO II, 11 JUNO I+II: the Juno
 * chorus, fx.c juno_chorus), its MIX, and the DELAY and REVERB sends. The delay's time and feedback are global
 * (GLO: TIME 1/8 dotted by default here), so a dub chord's long tail is the same delay as a lead's echo, only more
 * of it. */
#define HF(it, mix, dly, rev) {it, mix, dly, rev}
static const struct { uint8_t it, mix, dly, rev; } HOUSE_FX[] = {
    /* chords: organs, keys, stabs, pads */
    HF(0, 0, 0, 26), HF(0, 0, 0, 22), HF(0, 0, 0, 22), HF(0, 0, 0, 20),       /* HOUSE ORGN .. DIRTY B3 */
    HF(9, 50, 0, 30), HF(9, 60, 0, 28), HF(9, 60, 0, 28), HF(9, 60, 0, 24), HF(9, 60, 0, 24),   /* M1 PIANO .. SUITCASE */
    HF(0, 0, 80, 64),                                                          /* DUB CHORD: the long tail */
    HF(10, 70, 40, 36), HF(10, 70, 36, 34), HF(10, 70, 36, 34), HF(10, 70, 36, 34),   /* MIN7 .. SUS STAB */
    HF(0, 0, 30, 30), HF(10, 60, 30, 34), HF(10, 60, 30, 34),                  /* RAVE STAB, HOUSE CHRD, CHORD STAB */
    HF(11, 80, 0, 40), HF(0, 0, 0, 40), HF(11, 80, 0, 40),                   /* JUNO 106 PAD, SOLINA, WARM PAD */
    /* leads */
    HF(0, 0, 30, 34), HF(0, 0, 36, 22), HF(0, 0, 36, 22), HF(9, 50, 30, 22), HF(0, 0, 36, 22), HF(0, 0, 36, 22),
    HF(0, 0, 36, 22), HF(0, 0, 36, 22), HF(0, 0, 36, 22), HF(10, 60, 36, 24), HF(10, 60, 36, 24), HF(0, 0, 30, 26),
    HF(0, 0, 30, 26), HF(0, 0, 30, 16), HF(0, 0, 36, 22), HF(0, 0, 20, 40), HF(0, 0, 40, 34), HF(0, 0, 36, 30),
    HF(0, 0, 40, 34), HF(0, 0, 40, 30),
    /* basses: dry and tight (the 303s a touch of echo) */
    HF(0, 0, 0, 0), HF(0, 0, 0, 0), HF(0, 0, 0, 0), HF(0, 0, 0, 0), HF(0, 0, 0, 0), HF(0, 0, 0, 0), HF(0, 0, 0, 0),
    HF(0, 0, 0, 0), HF(0, 0, 0, 0), HF(0, 0, 0, 0), HF(0, 0, 24, 0), HF(0, 0, 24, 0), HF(0, 0, 24, 0), HF(0, 0, 24, 0),
    HF(0, 0, 24, 0), HF(0, 0, 0, 0), HF(0, 0, 0, 0), HF(0, 0, 0, 0), HF(0, 0, 0, 0), HF(0, 0, 0, 0),
};
static const int8_t HOUSE_TRIM[] = {3,4,4,2,4,3,4,4,3,4,4,4,4,4,4,4,4,5,4,5,4,9,4,5,1,4,4,7,8,4,4,5,6,3,4,5,6,4,4,4,-1,0,4,-5,1,5,0,-1,-5,1,4,-1,3,-1,1,-6,-3,-4,-5,-3};   /* loudness match, 1/2 dB */
_Static_assert(sizeof HOUSE_FX / sizeof HOUSE_FX[0] == NBANK, "a HOUSE_FX entry per BANK sound");
UI_SMALL static void house_apply(uint32_t bi, track_t *t)       /* the sound BANK[bi] was just loaded into t */
{
    if (bi >= NBANK)
        return;
#if CLIPPER_SYNTH
    t->p[P_ITYPE] = HOUSE_FX[bi].it;
    t->p[P_IMIX] = HOUSE_FX[bi].mix;
#endif
    t->p[P_ED_FX] = HOUSE_TRIM[bi];
    t->p[P_DLY] = HOUSE_FX[bi].dly;
    t->p[P_REV] = HOUSE_FX[bi].rev;
}

UI_SMALL static void preset_go(uint32_t n)                    /* load list index n into the selected track */
{
    uint32_t k, e = preset_at(n, &k), b[FAV_N], nf = fav_list(b);
    if (is_drum(TSEL))
        return;                                      /* one GM kit: nothing to browse */
    fav_at = (int8_t)(n < nf ? (int32_t)n : -1);
    if (e == NENGINES) {
        up_load(k);
        return;
    }
    if (e != TSEL->eng_req)
        select_engine(e);
    apply_preset(k);
    house_apply(n < nf ? b[n] : n - nf, TSEL);
    ui.force = 1;
}

/* 1.0 (a comment: "KNOB 2 just jumps to a preset of that engine, the list is not filtered"): the PRESETS page's
 * engine filter. KNOB 2 picks ALL, one engine (those with sounds in the list) or USER (the user presets); then KNOB 1,
 * the PRESETS knob on that page and the list shown go through that engine's sounds only, in the list's order (the
 * favourites, their own copies, are left out of a filter). HOME's PRESETS knob always browses the whole list */
#define BR_ALL 0xFFu
static uint8_t br_eng = BR_ALL;
UI_SMALL static int br_match(uint32_t n, uint32_t nf)         /* entry n of the list (nf favourites first) is in the filter */
{
    uint32_t k;
    if (br_eng == BR_ALL)
        return 1;
    return n >= nf && preset_at(n, &k) == br_eng;
}
/* the filtered list: how many, and where the sound now is in it (*pos; when it is not in it: 0) */
UI_SMALL static uint32_t br_count(uint32_t *pos)
{
    uint32_t total, cur = preset_pos(&total), b[FAV_N], nf = fav_list(b), n, c = 0;
    *pos = 0;
    for (n = 0; n < total; n++)
        if (br_match(n, nf)) {
            if (n == cur)
                *pos = c;
            c++;
        }
    return c;
}
/* the entry s matches on from list index n (back for s < 0), round; n itself when nothing matches */
UI_SMALL static uint32_t br_walk(uint32_t n, int32_t s)
{
    uint32_t total, b[FAV_N], nf = fav_list(b), guard;
    preset_pos(&total);
    if (!total)
        return 0;
    while (s) {
        guard = total;
        do
            n = s > 0 ? (n + 1u) % total : (n + total - 1u) % total;
        while (--guard && !br_match(n, nf));
        s += s > 0 ? -1 : 1;
    }
    return n;
}
UI_SMALL static void br_step(int32_t s)                        /* KNOB 1 / PRESETS on the PRESETS page */
{
    uint32_t total, cur = preset_pos(&total), b[FAV_N];
    if (is_drum(TSEL) || !total)
        return;
    if (!br_match(cur, fav_list(b)))                  /* (the sound now is not in the filter: its first one) */
        cur = br_walk(total - 1u, 1), s = s > 0 ? s - 1 : s;
    preset_go(br_walk(cur, s));
}
UI_SMALL static int br_engine_has(uint32_t e)                  /* the list holds sounds of engine e (NENGINES: user presets) */
{
    uint32_t total, n, k, b[FAV_N], nf = fav_list(b);
    preset_pos(&total);
    for (n = nf; n < total; n++)
        if (preset_at(n, &k) == e)
            return 1;
    return 0;
}
UI_SMALL static void br_filter_step(int32_t s)                 /* KNOB 2: ALL, the engines, USER, round */
{
    uint32_t e = br_eng == BR_ALL ? NENGINES + 1u : br_eng, guard = NENGINES + 2u, total, cur = preset_pos(&total), b[FAV_N];
    if (is_drum(TSEL))
        return;
    do                                                /* (0..NENGINES-1 the engines, NENGINES USER, NENGINES + 1 ALL) */
        e = s > 0 ? (e + 1u) % (NENGINES + 2u) : (e + NENGINES + 1u) % (NENGINES + 2u);
    while (--guard && e <= NENGINES && !br_engine_has(e));
    br_eng = (uint8_t)(e > NENGINES ? BR_ALL : e);
    if (!br_match(cur, fav_list(b)))
        preset_go(br_walk(total - 1u, 1));            /* (the filter's first sound) */
    {
        uint32_t pos, c = br_count(&pos);
        char m[24];
        str_cpy(m, br_eng == BR_ALL ? "ALL" : br_eng == NENGINES ? "USER" : ENGINES[br_eng]->name, sizeof m);
        str_cpy(m + str_len(m), " (", 3);
        fmt_int(m + str_len(m), (int32_t)c);
        str_cpy(m + str_len(m), ")", 2);
        ui_message(m);
    }
    ui.force = 1;
}

/* Clipper: ALGORITHM, the kind of sound: the first sound of the next kind (BASS, KEYS, ORGN, PAD, LEAD, PLCK,
 * STAB, FX, then the user presets), or of the previous one; round */
UI_SMALL static uint32_t kind_at(uint32_t n)                  /* (0.7: the favourites first, a kind of their own) */
{
    uint32_t b[FAV_N], nf = fav_list(b);
    return n < nf ? 0xFEu : n - nf < NBANK ? BANK[n - nf].kind : 0xFFu;
}
UI_SMALL static void preset_kind_jump(int32_t s)
{
    uint32_t total, cur = preset_pos(&total), n = cur, k;
    if (is_drum(TSEL) || total < 2u)
        return;
    while (n && kind_at(n - 1u) == kind_at(cur))        /* the start of the kind shown */
        n--;
    if (s > 0) {
        k = kind_at(cur);
        while (n < total && kind_at(n) == k)
            n++;
        if (n >= total)
            n = 0;
    } else {
        n = n ? n - 1u : total - 1u;                   /* into the kind before, then to its start */
        k = kind_at(n);
        while (n && kind_at(n - 1u) == k)
            n--;
    }
    preset_go(n);
    ui_message(preset_kind(n));
}

/* HOME: what KNOB k edits: the engine's four main parameters; on the drum track
 * LEVEL and REV (GLO > DRUMS), PAN and LEN */
UI_SMALL static const param_desc_t *home_param(uint32_t k, int16_t **vp)
{
    static const uint8_t DRUM_HOME[4][2] = {{1, G_DRLVL}, {1, G_DRREV}, {0, P_PAN}, {0, P_SLEN}};
    uint32_t id;
    if (is_drum(TSEL)) {
        id = DRUM_HOME[k & 3u][1];
        if (DRUM_HOME[k & 3u][0]) {
            *vp = &song.g[id];
            return &GP[id];
        }
        *vp = &TSEL->p[id];
        return &TP[id];
    }
    id = ENGINES[TSEL->eng_req % NENGINES]->macro[k & 3u];
    *vp = &TSEL->p[id];
    return track_desc(TSEL, id);
}

/* select track i (KNOB 1 on TRACKS, the editor): its sound, pages and pattern from now on */
UI_SMALL static void track_select(uint32_t i)
{
    if (i >= NTRK || i == song.sel || CLIPPER_SYNTH)   /* (Clipper: one instrument) */
        return;
    song.sel = (uint8_t)i;
    rec_follow(i);                                   /* LIVE: recording follows the selected track */
    ui.entry_open = 0;
    ui.cursor = 0;
    ui.bank = 0;
    sync_reload = 1;
    ui.force = 1;
}
