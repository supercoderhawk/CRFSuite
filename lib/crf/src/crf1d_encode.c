/*
 *      CRF1d encoder (routines for training).
 *
 * Copyright (c) 2007-2010, Naoaki Okazaki
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the names of the authors nor the names of its contributors
 *       may be used to endorse or promote products derived from this
 *       software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

 /* $Id$ */

#ifdef    HAVE_CONFIG_H
#include <config.h>
#endif/*HAVE_CONFIG_H*/

/* Libraries */
#include <os.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <time.h>

#include <crfsuite.h>
#include "crfsuite_internal.h"
#include "semimarkov.h"
#include "crf1d.h"
#include "params.h"
#include "logging.h"

/* Macros */
#define CLEAR(a_item)				\
  if ((a_item)) {				\
    free(a_item);				\
    a_item = NULL;				\
  }

#define    FEATURE(crf1de, k)			\
  (&(crf1de)->features[(k)])
#define    ATTRIBUTE(crf1de, a)			\
  (&(crf1de)->attributes[(a)])
#define    TRANSITION(crf1de, i)		\
  (&(crf1de)->forward_trans[(i)])

/* Interfaces */
/**
 * CRF1d internal data.
 */
typedef struct tag_crf1de crf1de_t;
struct tag_crf1de {
	int num_labels;	 /**< Number of distinct output labels (L). */
	int num_attributes;	 /**< Number of distinct attributes (A). */

	int cap_items;  /**< Maximum length of sequences in the data set. */

	int num_features;	      /**< Number of distinct features (K). */
	crf1df_feature_t *features; /**< Array of feature descriptors [K]. */
	feature_refs_t* attributes; /**< References to attribute features [A]. */
	feature_refs_t* forward_trans; /**< References to transition features [L]. */

	crf1d_context_t *ctx;		/**< CRF1d context. */
	crf1de_option_t opt;		/**< CRF1d options. */
	crf1de_semimarkov_t *sm;	/**< Data, specific to semi-markov model */

	/**
	 * Pointer to function for computing alpha score (the particular choice of
	 * this function will depend on the type of graphical model).
	 */
	void(*m_compute_alpha)(crf1d_context_t* a_ctx, const void *a_aux);

	/**
	 * Pointer to function for computing beta score (the particular choice of
	 * this function will depend on the type of graphical model).
	 */
	void(*m_compute_beta)(crf1d_context_t* a_ctx, const void *a_aux);

	/**
	 * Pointer to function for computing marginals.
	 */
	void(*m_compute_marginals)(crf1d_context_t* a_ctx, const void *a_aux);

	/**
	 * Pointer to function for finding most probable path.
	 */
	floatval_t(*m_viterbi)(crf1d_context_t* ctx, int *labels, const void *a_aux);

	/**
	 * Pointer to function for computing score of label sequence.
	 */
	floatval_t(*m_compute_score)(crf1d_context_t* a_ctx, const int *a_labels, const void *a_aux);

	/**
	 * Pointer to function for computing model score of features.
	 *
	 * @param crf1de - pointer to this encoder instance
	 * @param inst - pointer to training instance
	 * @param w - vector of feature weights to be populated
	 * @param scale - scaling factor for feature update
	 *
	 * @return \c void
	 */
	void(*m_model_expectation)(crf1de_t *crf1de, const crfsuite_instance_t *inst, \
		floatval_t *w, const floatval_t scale);

	/**
	 * Pointer to function for computing observation expectation.
	 *
	 * @param crf1de - pointer to this encoder instance
	 * @param inst - pointer to training instance
	 * @param labels - pointer to the sequence of correct labels
	 * @param aux - pointer to an auxiliary data structure (semi-Markov model or tree)
	 * @param scale - scaling factor
	 * @param w - pointer to the vector of feature weights
	 *
	 * @return \c void
	 */
	void(*m_observation_expectation)(crf1de_t* crf1de, const crfsuite_instance_t* inst, \
		const int *labels, const void *aux, const floatval_t scale, \
		floatval_t *w);

	/**
	 * Pointer to function for finding features pertaining to correct path.
	 *
	 * @param crf1de - pointer to this encoder instance
	 * @param inst - pointer to training instance
	 * @param labels - pointer to the sequence of correct labels
	 * @param aux - pointer to an auxiliary data structure (semi-Markov model or tree)
	 * @param func - callback function for feature evaluation
	 * @param w - pointer to the vector of feature weights
	 *
	 * @return \c void
	 */
	void(*m_features_on_path)(crf1de_t *crf1de, const crfsuite_instance_t *inst, const int *labels, \
		const void *aux, crfsuite_encoder_features_on_path_callback func, \
		void *instance);
};

/* Implementation */
static void crf1de_state_score(crf1de_t *crf1de,
	const crfsuite_instance_t* inst,
	const floatval_t* w)
{
	int i, t, r;
	crf1d_context_t* ctx = crf1de->ctx;
	const int T = inst->num_items;
	const crf1df_feature_t *f = NULL;

	/* Loop over the items in the sequence. */
	for (t = 0; t < T; ++t) {
		const crfsuite_item_t *item = &inst->items[t];
		floatval_t *state = STATE_SCORE(ctx, t);

		/* Loop over the contents (attributes) attached to the item. */
		for (i = 0; i < item->num_contents; ++i) {
			/* Access the list of state features associated with the attribute. */
			int a = item->contents[i].aid;
			const feature_refs_t *attr = ATTRIBUTE(crf1de, a);
			floatval_t value = item->contents[i].value;

			/* Loop over the state features associated with the attribute. */
			for (r = 0; r < attr->num_features; ++r) {
				/* State feature associates the attribute #a with the label #(f->dst). */
				int fid = attr->fids[r];
				f = FEATURE(crf1de, fid);
				state[f->dst] += w[fid] * value;
			}
		}
	}
}

static void
crf1de_state_score_scaled(
	crf1de_t* crf1de,
	const crfsuite_instance_t* inst,
	const floatval_t* w,
	const floatval_t scale
)
{
	int i, t, r;
	crf1d_context_t* ctx = crf1de->ctx;
	const int T = inst->num_items;
	const int L = crf1de->num_labels;

	/* Forward to the non-scaling version for fast computation when scale == 1. */
	if (scale == 1.) {
		crf1de_state_score(crf1de, inst, w);
		return;
	}

	/* Loop over the items in the sequence. */
	for (t = 0; t < T; ++t) {
		const crfsuite_item_t *item = &inst->items[t];
		floatval_t *state = STATE_SCORE(ctx, t);

		/* Loop over the contents (attributes) attached to the item. */
		for (i = 0; i < item->num_contents; ++i) {
			/* Access the list of state features associated with the attribute. */
			int a = item->contents[i].aid;
			const feature_refs_t *attr = ATTRIBUTE(crf1de, a);
			floatval_t value = item->contents[i].value * scale;

			/* Loop over the state features associated with the attribute. */
			for (r = 0; r < attr->num_features; ++r) {
				/* State feature associates the attribute #a with the label #(f->dst). */
				int fid = attr->fids[r];
				const crf1df_feature_t *f = FEATURE(crf1de, fid);
				state[f->dst] += w[fid] * value;
			}
		}
	}
}

static void crf1de_transition_score(crf1de_t* crf1de, const floatval_t* w, \
	const crf1de_semimarkov_t *sm)
{
	int i, r;
	floatval_t *trans = NULL;
	const feature_refs_t *edge = NULL;
	crf1d_context_t* ctx = crf1de->ctx;
	const int L = sm ? sm->m_num_frw : crf1de->num_labels;

	/* Compute transition scores between two labels. */
	for (i = 0; i < L; ++i) {
		trans = TRANS_SCORE(ctx, i);
		edge = TRANSITION(crf1de, i);
		for (r = 0; r < edge->num_features; ++r) {
			/* Transition feature from #i to #(f->dst). */
			int fid = edge->fids[r];
			const crf1df_feature_t *f = FEATURE(crf1de, fid);
			trans[f->dst] = w[fid];
		}
	}
}

static void crf1de_transition_score_scaled(crf1de_t* crf1de, const floatval_t* w, \
	const floatval_t scale)
{
	int i, r;
	crf1d_context_t* ctx = crf1de->ctx;
	const int L = crf1de->num_labels;

	/* Forward to the non-scaling version for fast computation when scale == 1. */
	if (scale == 1.) {
		crf1de_transition_score(crf1de, w, crf1de->sm);
		return;
	}

	/* Compute transition scores between two labels. */
	for (i = 0; i < L; ++i) {
		floatval_t *trans = TRANS_SCORE(ctx, i);
		const feature_refs_t *edge = TRANSITION(crf1de, i);
		for (r = 0; r < edge->num_features; ++r) {
			/* Transition feature from #i to #(f->dst). */
			int fid = edge->fids[r];
			const crf1df_feature_t *f = FEATURE(crf1de, fid);
			if (crf1de->sm)
				trans[crf1de->sm->m_ptrn_llabels[f->dst]] = w[fid] * scale;
			else
				trans[f->dst] = w[fid] * scale;
		}
	}
}

static inline void
crf1de_state_features_on_path(
	crf1de_t *crf1de,
	const crfsuite_item_t *item,
	const int cur,
	crfsuite_encoder_features_on_path_callback func,
	void *instance
)
{
	/* Loop over the contents (attributes) attached to the item. */
	for (int c = 0; c < item->num_contents; ++c) {
		/* Access the list of state features associated with the attribute. */
		int a = item->contents[c].aid;
		const feature_refs_t *attr = ATTRIBUTE(crf1de, a);
		floatval_t value = item->contents[c].value;

		/* Loop over the state features associated with the attribute. */
		for (int r = 0; r < attr->num_features; ++r) {
			/* State feature associates the attribute #a with the label #(f->dst). */
			int fid = attr->fids[r];
			const crf1df_feature_t *f = FEATURE(crf1de, fid);
			if (f->dst == cur)
				func(instance, fid, value);
		}
	}
}

static inline void
crf1de_transition_features_on_path(
	crf1de_t *crf1de,
	const int prev,
	const int cur,
	crfsuite_encoder_features_on_path_callback func,
	void *instance
)
{
	const feature_refs_t *edge = TRANSITION(crf1de, prev);
	for (int r = 0; r < edge->num_features; ++r) {
		/* Transition feature from #prev to #(f->dst). */
		int fid = edge->fids[r];
		const crf1df_feature_t *f = FEATURE(crf1de, fid);
		if (f->dst == cur)
			func(instance, fid, 1.);
	}
}

static void
crf1de_features_on_path(
	crf1de_t *crf1de,
	const crfsuite_instance_t *inst,
	const int *labels,
	const void *aux,
	crfsuite_encoder_features_on_path_callback func,
	void *instance
)
{
	int prev = -1, t;
	const int T = inst->num_items;

	/* Loop over the items in the sequence. */
	for (t = 0; t < T; ++t) {
		const int cur = labels[t];
		const crfsuite_item_t *item = &inst->items[t];

		crf1de_state_features_on_path(crf1de, item, cur, func, instance);

		if (prev != -1)
			crf1de_transition_features_on_path(crf1de, prev, cur, func, instance);

		prev = cur;
	}
}

static void
crf1de_tree_features_on_path(
	crf1de_t *crf1de,
	const crfsuite_instance_t *inst,
	const int *labels,
	const void *aux,
	crfsuite_encoder_features_on_path_callback func,
	void *instance
)
{
	const int T = inst->num_items;
	const crfsuite_item_t *item = NULL;
	const crfsuite_node_t *node, *child;
	const crfsuite_node_t *tree = (const crfsuite_node_t *)aux;

	int c, prev = -1, cur = -1, item_id, chld_item_id;
	for (int t = T - 1; t >= 0; --t) {
		node = &tree[t];
		item_id = node->self_item_id;
		item = &inst->items[item_id];

		cur = labels[item_id];
		crf1de_state_features_on_path(crf1de, item, cur, func, instance);

		for (c = 0; c < node->num_children; ++c) {
			child = &tree[node->children[c]];
			chld_item_id = child->self_item_id;
			prev = labels[chld_item_id];

			crf1de_transition_features_on_path(crf1de, prev, cur, func, instance);
		}
	}
}

static void
crf1de_sm_features_on_path(
	crf1de_t *crf1de,
	const crfsuite_instance_t *inst,
	const int *labels,
	const void *aux,
	crfsuite_encoder_features_on_path_callback func,
	void *instance
)
{
	exit(69);
}

static inline void
crf1de_state_observation_expectation(crf1de_t* crf1de,
	const crfsuite_item_t *item,
	const int cur,
	const floatval_t scale,
	floatval_t *w)
{
	int a, r;
	/* Loop over the contents (attributes) attached to the item. */
	for (int c = 0; c < item->num_contents; ++c) {
		/* Access the list of state features associated with the attribute. */
		a = item->contents[c].aid;
		const feature_refs_t *attr = ATTRIBUTE(crf1de, a);
		floatval_t value = item->contents[c].value;

		/* Loop over the state features associated with the attribute. */
		for (r = 0; r < attr->num_features; ++r) {
			/* State feature associates the attribute #a with the label #(f->dst). */
			int fid = attr->fids[r];
			const crf1df_feature_t *f = FEATURE(crf1de, fid);
			if (f->dst == cur) {
				w[fid] += value * scale;
			}
		}
	}
}

static inline void
crf1de_transition_observation_expectation(crf1de_t* crf1de,
	const int prev,
	const int cur,
	const floatval_t scale,
	floatval_t *w)
{
	const feature_refs_t *edge = TRANSITION(crf1de, prev);
	for (int r = 0; r < edge->num_features; ++r) {
		/* Transition feature from #i to #(f->dst). */
		int fid = edge->fids[r];
		const crf1df_feature_t *f = FEATURE(crf1de, fid);
		if (f->dst == cur) {
			w[fid] += scale;
		}
	}
}

static void
crf1de_observation_expectation(
	crf1de_t* crf1de,
	const crfsuite_instance_t* inst,
	const int *labels,
	const void *aux,
	const floatval_t scale,
	floatval_t *w
)
{
	int prev = -1;
	const int T = inst->num_items;

	/* Loop over the items in the sequence. */
	for (int t = 0; t < T; ++t) {
		const crfsuite_item_t *item = &inst->items[t];
		const int cur = labels[t];
		crf1de_state_observation_expectation(crf1de, item, cur, scale, w);

		if (prev != -1)
			crf1de_transition_observation_expectation(crf1de, prev, cur, scale, w);

		prev = cur;
	}
}

static void
crf1de_tree_observation_expectation(
	crf1de_t* crf1de,
	const crfsuite_instance_t* inst,
	const int *labels,
	const void *aux,
	const floatval_t scale,
	floatval_t *w
)
{
	const int T = inst->num_items;
	const crfsuite_item_t *item = NULL;
	const crfsuite_node_t *node, *child;
	const crfsuite_node_t *tree = (const crfsuite_node_t *)aux;

	int c, cur, prev, item_id, chld_item_id;
	for (int t = T - 1; t >= 0; --t) {
		node = &tree[t];
		item_id = node->self_item_id;
		item = &inst->items[item_id];

		cur = labels[t];
		crf1de_state_observation_expectation(crf1de, item, cur, scale, w);

		for (c = 0; c < node->num_children; ++c) {
			child = &tree[node->children[c]];
			chld_item_id = child->self_item_id;
			prev = labels[chld_item_id];
			crf1de_transition_observation_expectation(crf1de, prev, cur, scale, w);
		}
	}
}

static void
crf1de_sm_observation_expectation(
	crf1de_t* crf1de,
	const crfsuite_instance_t* inst,
	const int *labels,
	const void *aux,
	const floatval_t scale,
	floatval_t *w
)
{
	/* int prev = -1; */
	/* const int T = inst->num_items; */

	exit(66);
}

static void crf1de_model_expectation(crf1de_t *crf1de,
	const crfsuite_instance_t *inst,
	floatval_t *w,
	const floatval_t scale)
{
	int a, c, i, t, r;
	crf1d_context_t* ctx = crf1de->ctx;
	const feature_refs_t *attr = NULL, *trans = NULL;
	const crfsuite_item_t* item = NULL;
	const int T = inst->num_items;
	const int L = crf1de->num_labels;

	for (t = 0; t < T; ++t) {
		floatval_t *prob = STATE_MEXP(ctx, t);
		/* Compute expectations for state features at position #t. */
		item = &inst->items[t];
		for (c = 0; c < item->num_contents; ++c) {
			/* Access the attribute. */
			floatval_t value = item->contents[c].value;
			a = item->contents[c].aid;
			attr = ATTRIBUTE(crf1de, a);

			/* Loop over state features for the attribute. */
			for (r = 0; r < attr->num_features; ++r) {
				int fid = attr->fids[r];
				crf1df_feature_t *f = FEATURE(crf1de, fid);
				w[fid] += prob[f->dst] * value * scale;
			}
		}
	}

	/* Loop over the labels (t, i) */
	for (i = 0; i < L; ++i) {
		const floatval_t *prob = TRANS_MEXP(ctx, i);
		trans = TRANSITION(crf1de, i);
		for (r = 0; r < trans->num_features; ++r) {
			/* Transition feature from #i to #(f->dst). */
			int fid = trans->fids[r];
			crf1df_feature_t *f = FEATURE(crf1de, fid);
			w[fid] += prob[f->dst] * scale;
		}
	}
}

static void crf1de_sm_model_expectation(crf1de_t *crf1de,
	const crfsuite_instance_t *inst,
	floatval_t *w,
	const floatval_t scale)
{
	int a, c, i, t, r;
	crf1d_context_t* ctx = crf1de->ctx;
	crf1de_semimarkov_t *sm = crf1de->sm;
	const feature_refs_t *attr = NULL, *trans = NULL;
	const crfsuite_item_t* item = NULL;
	const int T = inst->num_items;

	for (t = 0; t < T; ++t) {
		floatval_t *prob = STATE_MEXP(ctx, t);
		/* Compute expectations for state features at position #t. */
		item = &inst->items[t];
		for (c = 0; c < item->num_contents; ++c) {
			/* Access the attribute. */
			floatval_t value = item->contents[c].value;
			a = item->contents[c].aid;
			attr = ATTRIBUTE(crf1de, a);

			/* Loop over state features for the attribute. */
			for (r = 0; r < attr->num_features; ++r) {
				int fid = attr->fids[r];
				crf1df_feature_t *f = FEATURE(crf1de, fid);
				w[fid] += prob[f->dst] * value * scale;
			}
		}
	}

	/* Loop over the labels (t, i) */
	const floatval_t *prob = NULL;
	for (i = 0; i < sm->m_num_frw; ++i) {
		prob = TRANS_MEXP(ctx, i);
		trans = TRANSITION(crf1de, i);

		for (r = 0; r < trans->num_features; ++r) {
			/* Transition feature from #i to #(f->dst). */
			int fid = trans->fids[r];
			crf1df_feature_t *f = FEATURE(crf1de, fid);
			w[fid] += prob[sm->m_ptrn_llabels[f->dst]] * scale;
		}
	}
}

static int crf1de_init(crf1de_t *crf1de, int ftype)
{
	crf1de->num_labels = 0;
	crf1de->num_attributes = 0;
	crf1de->cap_items = 0;
	crf1de->num_features = 0;
	crf1de->features = NULL;
	crf1de->attributes = NULL;
	crf1de->forward_trans = NULL;
	crf1de->sm = NULL;
	crf1de->ctx = NULL;
	crf1de->m_model_expectation = &crf1de_model_expectation;

	switch (ftype) {
	case FTYPE_CRF1TREE:
		crf1de->m_compute_alpha = &crf1dc_tree_alpha_score;
		crf1de->m_compute_beta = &crf1dc_tree_beta_score;
		crf1de->m_compute_marginals = &crf1dc_tree_marginals;
		crf1de->m_compute_score = &crf1dc_tree_score;
		crf1de->m_viterbi = &crf1dc_tree_viterbi;
		crf1de->m_observation_expectation = &crf1de_tree_observation_expectation;
		crf1de->m_features_on_path = &crf1de_tree_features_on_path;
		break;

	case FTYPE_SEMIMCRF:
		crf1de->sm = crf1de_create_semimarkov();
		if (crf1de->sm == NULL)
			return -1;

		crf1de->m_compute_alpha = &crf1dc_sm_alpha_score;
		crf1de->m_compute_beta = &crf1dc_sm_beta_score;
		crf1de->m_compute_marginals = &crf1dc_sm_marginals;
		crf1de->m_compute_score = &crf1dc_sm_score;
		crf1de->m_viterbi = &crf1dc_sm_viterbi;
		crf1de->m_model_expectation = &crf1de_sm_model_expectation;
		crf1de->m_observation_expectation = &crf1de_sm_observation_expectation;
		crf1de->m_features_on_path = &crf1de_sm_features_on_path;
		break;

	default:
		crf1de->m_compute_alpha = &crf1dc_alpha_score;
		crf1de->m_compute_beta = &crf1dc_beta_score;
		crf1de->m_compute_marginals = &crf1dc_marginals;
		crf1de->m_compute_score = &crf1dc_score;
		crf1de->m_viterbi = &crf1dc_viterbi;
		crf1de->m_observation_expectation = &crf1de_observation_expectation;
		crf1de->m_features_on_path = &crf1de_features_on_path;
	}
	return 0;
}

static void crf1de_finish(crf1de_t *crf1de)
{
	CLEAR(crf1de->ctx);
	CLEAR(crf1de->features);
	CLEAR(crf1de->attributes);
	CLEAR(crf1de->forward_trans);

	if (crf1de->sm) {
		crf1de->sm->clear(crf1de->sm);
		crf1de->sm = NULL;
	}
}

static int crf1de_set_data(crf1de_t *crf1de, \
	int ftype, \
	dataset_t *ds, \
	int num_labels, \
	int num_attributes, \
	logging_t *lg)
{
	int ret = 0;
	clock_t begin = 0;
	int T = 0;
	const int L = num_labels;
	const int A = num_attributes;

	/* Initialize member variables. */
	crf1de->num_attributes = A;
	crf1de->num_labels = L;

	/* Generate features. */
	const crf1de_option_t *opt = &crf1de->opt;
	logging(lg, "Feature generation\n");
	switch (ftype) {
	case FTYPE_CRF1TREE:
		logging(lg, "type: %s\n", "crf1tree");
		break;

	case FTYPE_SEMIMCRF:
		if (opt->feature_max_seg_len < 0)
			logging(lg, "type: %s (%d order)\n", "semimarkov", opt->feature_max_order);
		else
			logging(lg, "type: %s (%d order)\n", "crf1d", opt->feature_max_order);
		break;

	default:
		logging(lg, "type: %s (1-st order)\n", "crf1d");
		break;
	}
	logging(lg, "feature.minfreq: %f\n", opt->feature_minfreq);
	logging(lg, "feature.possible_states: %d\n", opt->feature_possible_states);
	logging(lg, "feature.possible_transitions: %d\n", opt->feature_possible_transitions);
	begin = clock();

	crf1de->features = crf1df_generate(&crf1de->num_features,
		crf1de->sm,
		&T,
		opt,
		ds,
		ftype,
		L,
		lg->func,
		lg->instance);

	if (crf1de->features == NULL) {
		ret = CRFSUITEERR_OUTOFMEMORY;
		goto error_exit;
	}
	logging(lg, "Number of features: %d\n", crf1de->num_features);
	logging(lg, "Seconds required: %.3f\n", (clock() - begin) / (double)CLOCKS_PER_SEC);
	logging(lg, "\n");

	/* Initialize feature references. */
	crf1df_init_references(&crf1de->attributes,
		&crf1de->forward_trans,
		crf1de->features,
		crf1de->sm,
		crf1de->num_features,
		A,
		L);

	if (crf1de->attributes == NULL || crf1de->forward_trans == NULL) {
		ret = CRFSUITEERR_OUTOFMEMORY;
		goto error_exit;
	}

	/* Construct CRF context. */
	crf1de->ctx = crf1dc_new(CTXF_MARGINALS | CTXF_VITERBI, ftype, L, T, crf1de->sm);
	if (crf1de->ctx == NULL) {
		ret = CRFSUITEERR_OUTOFMEMORY;
		goto error_exit;
	}
	return ret;

error_exit:
	crf1de_finish(crf1de);
	return ret;
}

static int
crf1de_save_model(
	crf1de_t *crf1de,
	const char *filename,
	const floatval_t *w,
	crfsuite_dictionary_t *attrs,
	crfsuite_dictionary_t *labels,
	const int ftype,
	logging_t *lg
)
{
	int a, k, l, ret = 0;
	clock_t begin;
	int *fmap = NULL, *amap = NULL;
	crf1dmw_t* writer = NULL;
	const feature_refs_t *edge = NULL, *attr = NULL;
	const int L = crf1de->num_labels;
	const int A = crf1de->num_attributes;
	const int K = crf1de->num_features;
	int J = 0, B = 0;

	/* Start storing the model. */
	logging(lg, "Storing the model\n");
	begin = clock();

	/* Allocate and initialize the feature mapping. */
	fmap = (int*)calloc(K, sizeof(int));
	if (fmap == NULL) {
		goto error_exit;
	}
#ifdef  CRF_TRAIN_SAVE_NO_PRUNING
	for (k = 0; k < K; ++k) fmap[k] = k;
	J = K;
#else
	for (k = 0; k < K; ++k) fmap[k] = -1;
#endif/*CRF_TRAIN_SAVE_NO_PRUNING*/

	/* Allocate and initialize the attribute mapping. */
	amap = (int*)calloc(A, sizeof(int));
	if (amap == NULL) {
		goto error_exit;
	}
#ifdef  CRF_TRAIN_SAVE_NO_PRUNING
	for (a = 0; a < A; ++a) amap[a] = a;
	B = A;
#else
	for (a = 0; a < A; ++a) amap[a] = -1;
#endif/*CRF_TRAIN_SAVE_NO_PRUNING*/

	/*
	 *  Open a model writer.
	 */
	writer = crf1mmw(filename, ftype);
	if (writer == NULL)
		goto error_exit;

	/* Open a feature chunk in the model file. */
	if ((ret = crf1dmw_open_features(writer)))
		goto error_exit;

	/*
	 * Write the feature values.
	 * (with determining active features and attributes).
	 */
	for (k = 0; k < K; ++k) {
		crf1df_feature_t* f = &crf1de->features[k];
		if (w[k] != 0) {
			int src;
			crf1dm_feature_t feat;

#ifndef CRF_TRAIN_SAVE_NO_PRUNING
			/* The feature (#k) will have a new feature id (#J). */
			fmap[k] = J++;        /* Feature #k -> #fmap[k]. */

			/* Map the source of the field. */
			if (f->type == FT_STATE) {
				/* The attribute #(f->src) will have a new attribute id (#B). */
				if (amap[f->src] < 0) amap[f->src] = B++;    /* Attribute #a -> #amap[a]. */
				src = amap[f->src];
			}
			else {
				src = f->src;
			}
#endif/*CRF_TRAIN_SAVE_NO_PRUNING*/

			feat.type = f->type;
			feat.src = src;
			feat.dst = f->dst;
			feat.weight = w[k];

			/* Write the feature. */
			if ((ret = crf1dmw_put_feature(writer, fmap[k], &feat))) {
				goto error_exit;
			}
		}
	}

	/* Close the feature chunk. */
	if ((ret = crf1dmw_close_features(writer))) {
		goto error_exit;
	}

	logging(lg, "Number of active features: %d (%d)\n", J, K);
	logging(lg, "Number of active attributes: %d (%d)\n", B, A);
	logging(lg, "Number of active labels: %d (%d)\n", L, L);

	/* Write labels. */
	logging(lg, "Writing labels\n", L);
	if ((ret = crf1dmw_open_labels(writer, L))) {
		goto error_exit;
	}
	for (l = 0; l < L; ++l) {
		const char *str = NULL;
		labels->to_string(labels, l, &str);
		if (str != NULL) {
			if ((ret = crf1dmw_put_label(writer, l, str))) {
				goto error_exit;
			}
			labels->free(labels, str);
		}
	}
	if ((ret = crf1dmw_close_labels(writer))) {
		goto error_exit;
	}

	/* Write attributes. */
	logging(lg, "Writing attributes\n");
	if ((ret = crf1dmw_open_attrs(writer, B))) {
		goto error_exit;
	}
	for (a = 0; a < A; ++a) {
		if (0 <= amap[a]) {
			const char *str = NULL;
			attrs->to_string(attrs, a, &str);
			if (str != NULL) {
				if ((ret = crf1dmw_put_attr(writer, amap[a], str))) {
					goto error_exit;
				}
				attrs->free(attrs, str);
			}
		}
	}
	if ((ret = crf1dmw_close_attrs(writer))) {
		goto error_exit;
	}

	/* Write label feature references. */
	logging(lg, "Writing feature references for transitions\n");
	const int num_labels = (ftype == FTYPE_SEMIMCRF) ? crf1de->sm->m_num_frw : L;

	if ((ret = crf1dmw_open_labelrefs(writer, num_labels + 2))) {
		goto error_exit;
	}
	for (l = 0; l < num_labels; ++l) {
		edge = TRANSITION(crf1de, l);
		if ((ret = crf1dmw_put_labelref(writer, l, edge, fmap))) {
			goto error_exit;
		}
	}
	if ((ret = crf1dmw_close_labelrefs(writer))) {
		goto error_exit;
	}

	/* Write attribute feature references. */
	logging(lg, "Writing feature references for attributes\n");
	if ((ret = crf1dmw_open_attrrefs(writer, B))) {
		goto error_exit;
	}
	for (a = 0; a < A; ++a) {
		if (0 <= amap[a]) {
			attr = ATTRIBUTE(crf1de, a);
			if ((ret = crf1dmw_put_attrref(writer, amap[a], attr, fmap))) {
				goto error_exit;
			}
		}
	}
	if ((ret = crf1dmw_close_attrrefs(writer))) {
		goto error_exit;
	}

	/* Write data specific to semi-markov model. */
	if (ftype == FTYPE_SEMIMCRF) {
		logging(lg, "Storing semi-markov data\n");
		crf1de_semimarkov_t *sm = crf1de->sm;
		if ((ret = crf1dmw_open_sm(writer, sm)))
			goto error_exit;

		/* write states */
		const size_t s_max = sm->m_num_frw;
		for (size_t s = 0; s < s_max; ++s) {
			if ((ret = crf1dmw_put_sm_state(writer, s, &sm->m_frw_states[s], sm))) {
				goto error_exit;
			}
		}

		if ((ret = crf1dmw_close_sm(writer)))
			goto error_exit;

	}
	else {
		writer->header.off_sm = 0;
	}

	/* Close the writer. */
	crf1dmw_close(writer);
	logging(lg, "Seconds required: %.3f\n", (clock() - begin) / (double)CLOCKS_PER_SEC);
	logging(lg, "\n");

	free(amap);
	free(fmap);
	return 0;

error_exit:
	if (writer)
		crf1dmw_close(writer);

	if (amap)
		free(amap);

	if (fmap)
		free(fmap);

	return ret;
}

static int crf1de_exchange_options(crfsuite_params_t* params, crf1de_option_t* opt, int mode, \
	int ftype)
{
	BEGIN_PARAM_MAP(params, mode)
		DDX_PARAM_FLOAT(
			"feature.minfreq", opt->feature_minfreq, 0.0,
			"The minimum frequency of features."
		)
		DDX_PARAM_INT(
			"feature.possible_states", opt->feature_possible_states, 0,
			"Force to generate possible state features."
		)
		DDX_PARAM_INT(
			"feature.possible_transitions", opt->feature_possible_transitions, 0,
			"Force to generate possible transition features."
		)
		if (ftype == FTYPE_SEMIMCRF) {
			DDX_PARAM_INT(
				"feature.max_seg_len", opt->feature_max_seg_len, -1,
				"Constraint on maximum length of sequences with same tags (0< means infinite)."
			)

				DDX_PARAM_INT(
					"feature.max_order", opt->feature_max_order, 1,
					"Maximum order of transition features."
				)
		}

	END_PARAM_MAP()

	return __ret;
}



/*
 *    Implementation of encoder_t object.
 */

enum {
	/** No precomputation. */
	LEVEL_NONE = 0,
	/** Feature weights are set. */
	LEVEL_WEIGHT,
	/** Instance is set. */
	LEVEL_INSTANCE,
	/** Performed the forward-backward algorithm. */
	LEVEL_ALPHABETA,
	/** Computed marginal probabilities. */
	LEVEL_MARGINAL,
};

static void set_level(encoder_t *self, int level, const void *aux)
{
	int prev = self->level;
	crf1de_t *crf1de = (crf1de_t*)self->internal;

	/*
	  Each training algorithm has a different requirement for processing a
	  training instance. For example, the perceptron algorithm need compute
	  Viterbi paths whereas gradient-based algorithms (e.g., SGD) need
	  marginal probabilities computed by the forward-backward algorithm.
	*/

	/* LEVEL_WEIGHT: set transition scores. */
	if (LEVEL_WEIGHT <= level && prev < LEVEL_WEIGHT) {
		crf1dc_reset(crf1de->ctx, RF_TRANS, crf1de->sm);
		crf1de_transition_score_scaled(crf1de, self->w, self->scale);
	}

	/* LEVEL_INSTANCE: set state scores. */
	if (LEVEL_INSTANCE <= level && prev < LEVEL_INSTANCE) {
		crf1dc_set_num_items(crf1de->ctx, crf1de->sm, self->inst->num_items);
		crf1dc_reset(crf1de->ctx, RF_STATE, crf1de->sm);
		crf1de_state_score_scaled(crf1de, self->inst, self->w, self->scale);
	}

	/* LEVEL_ALPHABETA: perform the forward-backward algorithm. */
	if (LEVEL_ALPHABETA <= level && prev < LEVEL_ALPHABETA) {
		crf1dc_exp_transition(crf1de->ctx, crf1de->sm);
		crf1dc_exp_state(crf1de->ctx);
		crf1de->m_compute_alpha(crf1de->ctx, aux);
		crf1de->m_compute_beta(crf1de->ctx, aux);
	}

	/* LEVEL_MARGINAL: compute the marginal probability. */
	if (LEVEL_MARGINAL <= level && prev < LEVEL_MARGINAL) {
		crf1de->m_compute_marginals(crf1de->ctx, aux);
	}

	self->level = level;
}

static int encoder_exchange_options(encoder_t *self, crfsuite_params_t* params, int mode)
{
	crf1de_t *crf1de = (crf1de_t*)self->internal;
	return crf1de_exchange_options(params, &crf1de->opt, mode, self->ftype);
}

static int encoder_initialize(encoder_t *self, int ftype, dataset_t *ds, logging_t *lg)
{
	int ret = 0;
	crf1de_t *crf1de = (crf1de_t*)self->internal;
	if ((ret = crf1de_init(crf1de, ftype)))
		return ret;

	if (!ds->data || !ds->data->labels || !ds->data->attrs)
		return 2;

	ret = crf1de_set_data(crf1de,
		ftype,
		ds,
		ds->data->labels->num(ds->data->labels),
		ds->data->attrs->num(ds->data->attrs),
		lg);

	self->ds = ds;
	if (!ret) {
		self->num_features = crf1de->num_features;
		self->cap_items = crf1de->ctx->cap_items;
	}
	return ret;
}

/* LEVEL_NONE -> LEVEL_NONE. */
static int encoder_objective_and_gradients_batch(encoder_t *self, \
	dataset_t *ds, \
	const floatval_t *w, \
	floatval_t *f, \
	floatval_t *g)
{
	/* static int rnd_cnt = 0; */
	int i;
	floatval_t logp = 0, logl = 0;
	floatval_t model_score = 0., log_norm = 0.;
	crf1de_t *crf1de = (crf1de_t*)self->internal;
	const int N = ds->num_instances;
	const int K = crf1de->num_features;

	/*
	 * Initialize gradients with observation expectations.
	 */
	for (i = 0; i < K; ++i)
		g[i] = -crf1de->features[i].freq;


	/* Set the scores (weights) of transition features here because */
	/* these are independent of input label sequences. */
	crf1dc_reset(crf1de->ctx, RF_TRANS, crf1de->sm); /* reset transition table */
	crf1de_transition_score(crf1de, w, crf1de->sm); /* populate transition table */
	/* do not exponentiate transitions for semi-markov model */
	if (self->ftype != FTYPE_SEMIMCRF)
		crf1dc_exp_transition(crf1de->ctx, crf1de->sm); /* simply exponentiate transition scores */

	  /*
	   * Compute model expectations.
	   */
	const void *aux = NULL;
	if (self->ftype == FTYPE_SEMIMCRF)
		aux = (const void *)crf1de->sm;

	for (i = 0; i < N; ++i) {
		const crfsuite_instance_t *seq = dataset_get(ds, i);
		if (self->ftype == FTYPE_CRF1TREE)
			aux = (const void *)seq->tree;

		/* Set label sequences and state scores. */
		crf1dc_set_num_items(crf1de->ctx, crf1de->sm, seq->num_items);
		crf1dc_reset(crf1de->ctx, RF_STATE, crf1de->sm);
		crf1de_state_score(crf1de, seq, w);
		/* don't exponentiate state scores for semi-markov model */
		if (self->ftype != FTYPE_SEMIMCRF)
			crf1dc_exp_state(crf1de->ctx);

		/* Compute forward/backward scores. */
		crf1de->m_compute_alpha(crf1de->ctx, aux);
		crf1de->m_compute_beta(crf1de->ctx, aux);
		crf1de->m_compute_marginals(crf1de->ctx, aux);

		/* Compute probability of the input sequence on the model. */
		model_score = crf1de->m_compute_score(crf1de->ctx, seq->labels, aux);
		log_norm = crf1dc_lognorm(crf1de->ctx);
		assert(model_score <= log_norm);
		logp = model_score - log_norm;
		/* Update log-likelihood. */
		logl += logp;

		/* Update model expectations of features. */
		crf1de->m_model_expectation(crf1de, seq, g, 1.);
	}
	*f = -logl;
	return 0;
}

/* LEVEL_NONE -> LEVEL_NONE. */
static int encoder_features_on_path(encoder_t *self, \
	const crfsuite_instance_t *inst, \
	const int *path, \
	const void *aux, \
	crfsuite_encoder_features_on_path_callback func, \
	void *instance)
{
	crf1de_t *crf1de = (crf1de_t*)self->internal;
	crf1de->m_features_on_path(crf1de, inst, path, aux, func, instance);
	return 0;
}

/* LEVEL_NONE -> LEVEL_NONE. */
static int encoder_save_model(encoder_t *self, const char *filename, \
	const floatval_t *w, logging_t *lg)
{
	crf1de_t *crf1de = (crf1de_t*)self->internal;
	return crf1de_save_model(crf1de, filename, w, self->ds->data->attrs, \
		self->ds->data->labels, self->ftype, lg);
}

/* LEVEL_NONE -> LEVEL_WEIGHT. */
static int encoder_set_weights(encoder_t *self, const floatval_t *w, floatval_t scale)
{
	self->w = w;
	self->scale = scale;
	self->level = LEVEL_WEIGHT - 1;
	set_level(self, LEVEL_WEIGHT, NULL);
	return 0;
}

/* LEVEL_WEIGHT -> LEVEL_INSTANCE. */
static int encoder_set_instance(encoder_t *self, const crfsuite_instance_t *inst)
{
	self->inst = inst;
	self->level = LEVEL_INSTANCE - 1;
	set_level(self, LEVEL_INSTANCE, NULL);
	return 0;
}

/* LEVEL_INSTANCE -> LEVEL_INSTANCE. */
static int encoder_score(encoder_t *self, const int *path, floatval_t *ptr_score, \
	const void *aux)
{
	crf1de_t *crf1de = (crf1de_t*)self->internal;
	*ptr_score = crf1de->m_compute_score(crf1de->ctx, path, aux);
	return 0;
}

/* LEVEL_INSTANCE -> LEVEL_INSTANCE. */
static int encoder_viterbi(encoder_t *self, int *path, floatval_t *ptr_score, \
	const void *aux)
{
	crf1de_t *crf1de = (crf1de_t*)self->internal;
	floatval_t score = crf1de->m_viterbi(crf1de->ctx, path, aux);
	if (ptr_score)
		*ptr_score = score;

	return 0;
}

/* LEVEL_INSTANCE -> LEVEL_ALPHABETA. */
static int encoder_partition_factor(encoder_t *self, floatval_t *ptr_pf, const void *aux)
{
	crf1de_t *crf1de = (crf1de_t*)self->internal;
	set_level(self, LEVEL_ALPHABETA, aux);
	*ptr_pf = crf1dc_lognorm(crf1de->ctx);
	return 0;
}

/* LEVEL_INSTANCE -> LEVEL_MARGINAL. */
static int encoder_objective_and_gradients(encoder_t *self, floatval_t *f, floatval_t *g, \
	floatval_t gain, const void *aux)
{
	crf1de_t *crf1de = (crf1de_t*)self->internal;
	if (!aux && self->ftype == FTYPE_SEMIMCRF)
		aux = (const void *)crf1de->sm;

	set_level(self, LEVEL_MARGINAL, aux);
	crf1de->m_observation_expectation(crf1de, self->inst, self->inst->labels, aux, gain, g);
	crf1de->m_model_expectation(crf1de, self->inst, g, -gain);
	*f = -crf1de->m_compute_score(crf1de->ctx, self->inst->labels, aux) + \
		crf1dc_lognorm(crf1de->ctx);
	return 0;
}

static void encoder_delete(encoder_t *self)
{
	crf1de_t *enc = (crf1de_t *)self->internal;
	crf1de_finish(enc);
}

encoder_t *crf1d_create_encoder(int ftype)
{
	void *enc = NULL;
	encoder_t *self = (encoder_t*)calloc(1, sizeof(encoder_t));
	if (self != NULL) {
		enc = (crf1de_t*)calloc(1, sizeof(crf1de_t));

		if (enc != NULL) {
			if (crf1de_init(enc, ftype))
				goto error_exit;

			self->ftype = ftype;
			self->delete = encoder_delete;
			self->exchange_options = encoder_exchange_options;
			self->initialize = encoder_initialize;
			self->objective_and_gradients_batch = encoder_objective_and_gradients_batch;
			self->save_model = encoder_save_model;
			self->features_on_path = encoder_features_on_path;
			self->set_weights = encoder_set_weights;
			self->set_instance = encoder_set_instance;
			self->score = encoder_score;
			self->viterbi = encoder_viterbi;
			self->partition_factor = encoder_partition_factor;
			self->objective_and_gradients = encoder_objective_and_gradients;
			self->internal = enc;
		}
	}
	return self;

error_exit:
	free((crf1de_t*)enc);
	free(self);
	return NULL;
}
