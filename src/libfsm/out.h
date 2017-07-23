/*
 * Copyright 2008-2017 Katherine Flavel
 *
 * See LICENCE for the full copyright terms.
 */

#ifndef FSM_INTERNAL_OUT_H
#define FSM_INTERNAL_OUT_H

#include <stdio.h>

#include <fsm/out.h>

void
fsm_out_stateenum(FILE *f, const struct fsm *fsm, struct fsm_state *sl);

void
fsm_out_api(const struct fsm *fsm, FILE *f);

void
fsm_out_c(const struct fsm *fsm, FILE *f);

void
fsm_out_csv(const struct fsm *fsm, FILE *f);

void
fsm_out_dot(const struct fsm *fsm, FILE *f);

void
fsm_out_fsm(const struct fsm *fsm, FILE *f);

void
fsm_out_json(const struct fsm *fsm, FILE *f);

#endif

