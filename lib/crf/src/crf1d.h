/*
 *      The 1st-order linear-chain CRF with dyad features (CRF1d).
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

#ifndef    __CRF1D_H__
#define    __CRF1D_H__

#include <cqdb.h>
#include <crfsuite.h>
#include <stdint.h>

#include "crfsuite_internal.h"
#include "semimarkov.h"


/**
 * \defgroup crf1d_context.c
 */
 /** @{ */

 /**
  * Functionality flags for contexts.
  *  @see    crf1dc_new().
  */
enum {
	CTXF_BASE = 0x01,
	CTXF_VITERBI = 0x01,
	CTXF_MARGINALS = 0x02,
	CTXF_ALL = 0xFF,
};

/**
 * Reset flags.
 *  @see    crf1dc_reset().
 */
enum {
	RF_STATE = 0x01,     /**< Reset state scores. */
	RF_TRANS = 0x02,     /**< Reset transition scores. */
	RF_ALL = 0xFF,     /**< Reset all. */
};

/**
 * Context structure.
 *  This structure maintains internal data for an instance.
 */
typedef struct {
	/**
	 * Flag specifying the type of the graphical model.
	 */
	int ftype;

	/**
	 * Flag specifying the functionality.
	 */
	int flag;

	/**
	 * The total number of distinct labels (L).
	 */
	int num_labels;

	/**
	 * The number of items (T) in the instance.
	 */
	int num_items;

	/**
	 * The maximum number of labels.
	 */
	int cap_items;

	/**
	 * Logarithm of the normalization factor for the instance.
	 *  This is equivalent to the total scores of all paths in the lattice.
	 */
	floatval_t log_norm;

	/**
	 * State scores.
	 *  This is a [T][L] matrix whose element [t][l] presents total score
	 *  of state features associating label #l at #t.
	 */
	floatval_t *state;

	/**
	 * Transition scores.
	 *  This is a [L][L] matrix whose element [i][j] represents the total
	 *  score of transition features associating labels #i and #j.
	 */
	floatval_t *trans;

	/**
	 * Alpha score matrix.
	 *  This is a [T][L] matrix whose element [t][l] presents the total
	 *  score of paths starting at BOS and arriving at (t, l).
	 */
	floatval_t *alpha_score;

	/**
	 * Unnormalized alpha score propagated from child to parent.
	 *
	 *  This is a [T][L] matrix whose element [t][l] presents the unscaled alpha
	 *  score that child t propagates to its parent.
	 */
	floatval_t *child_alpha_score;

	/**
	 * Beta score matrix.
	 *  This is a [T][L] matrix whose element [t][l] presents the total
	 *  score of paths starting at (t, l) and arriving at EOS.
	 */
	floatval_t *beta_score;

	/**
	 * Scale factor vector.
	 *  This is a [T] vector whose element [t] presents the scaling
	 *  coefficient for the alpha_score and beta_score.
	 */
	floatval_t *scale_factor;

	/**
	 * Row vector (work space).
	 *  This is a [L] vector used internally for a work space.
	 */
	floatval_t *row;

	/**
	 * Backward edges.
	 *  This is a [T][L] matrix whose element [t][j] represents the label #i
	 *  that yields the maximum score to arrive at (t, j).
	 *  This member is available only with CTXF_VITERBI flag enabled.
	 *
	 *  For tree-structured CRFs, the semantics of this container is re-defined.
	 *  Cell [t][i] holds the tag of t-th node which leads to the highest
	 *  probability of tag `i` for t's parent.
	 */
	int *backward_edge;

	/**
	 * Backward end.
	 *
	 *  This [T][L] matrix is only used by semi-markov model in conjunction with
	 *  the `backward_edge` matrix.  Whereas the `backward_edge` matrix holds
	 *  the column index of the previous state, that has lead to the maximum
	 *  possible score of the given tagging sequence at the given position `t`,
	 *  this matrix holds the row (i.e. the ending point) of this previous state
	 *  which in contrast to other model types does not necessarily need to be
	 *  `t - 1`.
	 *  This member is available only with CTXF_VITERBI flag enabled.
	 */
	int *backward_end;

	/**
	 * Exponents of state scores.
	 *  This is a [T][L] matrix whose element [t][l] presents the exponent
	 *  of the total score of state features associating label #l at #t.
	 *  This member is available only with CTXF_MARGINALS flag.
	 */
	floatval_t *exp_state;

	/**
	 * Exponents of transition scores.
	 *  This is a [L][L] matrix whose element [i][j] represents the exponent
	 *  of the total score of transition features associating labels #i and #j.
	 *  This member is available only with CTXF_MARGINALS flag.
	 */
	floatval_t *exp_trans;

	/**
	 * Model expectations of states.
	 *  This is a [T][L] matrix whose element [t][l] presents the model
	 *  expectation (marginal probability) of the state (t,l)
	 *  This member is available only with CTXF_MARGINALS flag.
	 */
	floatval_t *mexp_state;

	/**
	 * Model expectations of transitions.
	 *  This is a [L][L] matrix whose element [i][j] presents the model
	 *  expectation of the transition (i--j).
	 *  This member is available only with CTXF_MARGINALS flag.
	 */
	floatval_t *mexp_trans;

} crf1d_context_t;

#define    MATRIX(p, xl, x, y)        ((p)[(xl) * (y) + (x)])

#define    ALPHA_SCORE(ctx, t)				\
  (&MATRIX(ctx->alpha_score, ctx->num_labels, 0, t))
#define    CHILD_ALPHA_SCORE(ctx, t)				\
  (&MATRIX(ctx->child_alpha_score, ctx->num_labels, 0, t))
/*! obtain alpha score column for semi-markov model */
#define    SM_ALPHA_SCORE(ctx, sm, t)			\
  (&MATRIX(ctx->alpha_score, sm->m_num_frw, 0, t))

#define    BETA_SCORE(ctx, t)				\
  (&MATRIX(ctx->beta_score, ctx->num_labels, 0, t))
/*! obtain beta score column for semi-markov model */
#define    SM_BETA_SCORE(ctx, sm, t)			\
  (&MATRIX(ctx->beta_score, sm->m_num_bkw, 0, t))

#define    STATE_SCORE(ctx, i)			\
  (&MATRIX(ctx->state, ctx->num_labels, 0, i))

#define    TRANS_SCORE(ctx, i)			\
  (&MATRIX(ctx->trans, ctx->num_labels, 0, i))

#define    EXP_STATE_SCORE(ctx, i)			\
  (&MATRIX(ctx->exp_state, ctx->num_labels, 0, i))
#define    EXP_TRANS_SCORE(ctx, i)			\
  (&MATRIX(ctx->exp_trans, ctx->num_labels, 0, i))
#define    STATE_MEXP(ctx, i)				\
  (&MATRIX(ctx->mexp_state, ctx->num_labels, 0, i))
#define    TRANS_MEXP(ctx, i)				\
  (&MATRIX(ctx->mexp_trans, ctx->num_labels, 0, i))
#define    BACKWARD_EDGE_AT(ctx, t)			\
  (&MATRIX(ctx->backward_edge, ctx->num_labels, 0, t))
#define    SM_BACKWARD_EDGE_AT(ctx, sm, t)		\
  (&MATRIX(ctx->backward_edge, sm->m_num_frw, 0, t))
#define    SM_BACKWARD_END_AT(ctx, sm, t)		\
  (&MATRIX(ctx->backward_end, sm->m_num_frw, 0, t))

crf1d_context_t* crf1dc_new(int flag, const int ftype, int L, int T, const crf1de_semimarkov_t *sm);
int crf1dc_set_num_items(crf1d_context_t* ctx, const crf1de_semimarkov_t *sm, const int T);
void crf1dc_delete(crf1d_context_t* ctx);
void crf1dc_reset(crf1d_context_t* ctx, int flag, const crf1de_semimarkov_t *sm);
void crf1dc_exp_state(crf1d_context_t* ctx);
void crf1dc_exp_transition(crf1d_context_t* ctx, const crf1de_semimarkov_t *sm);

void crf1dc_alpha_score(crf1d_context_t* a_ctx, const void *a_aux);
void crf1dc_tree_alpha_score(crf1d_context_t* a_ctx, const void *a_aux);
void crf1dc_sm_alpha_score(crf1d_context_t* a_ctx, const void *a_aux);

void crf1dc_beta_score(crf1d_context_t* a_ctx, const void *a_aux);
void crf1dc_tree_beta_score(crf1d_context_t* a_ctx, const void *a_aux);
void crf1dc_sm_beta_score(crf1d_context_t* a_ctx, const void *a_aux);

void crf1dc_marginals(crf1d_context_t* a_ctx, const void *a_aux);
void crf1dc_tree_marginals(crf1d_context_t* a_ctx, const void *a_aux);
void crf1dc_sm_marginals(crf1d_context_t* a_ctx, const void *a_aux);

floatval_t crf1dc_score(crf1d_context_t* a_ctx, const int *a_labels, const void *a_aux);
floatval_t crf1dc_tree_score(crf1d_context_t* a_ctx, const int *a_labels, const void *a_aux);
floatval_t crf1dc_sm_score(crf1d_context_t* a_ctx, const int *a_labels, const void *a_aux);

floatval_t crf1dc_marginal_point(crf1d_context_t *ctx, int l, int t);
floatval_t crf1dc_marginal_path(crf1d_context_t *ctx, const int *path, \
	int begin, int end, const void *aux);
floatval_t crf1dc_tree_marginal_path(crf1d_context_t *ctx, const int *path, \
	int begin, int end, const void *aux);
floatval_t crf1dc_sm_marginal_path(crf1d_context_t *ctx, const int *path, \
	int begin, int end, const void *aux);

floatval_t crf1dc_lognorm(crf1d_context_t* ctx);

floatval_t crf1dc_viterbi(crf1d_context_t* ctx, int *labels, const void *a_aux);
floatval_t crf1dc_tree_viterbi(crf1d_context_t* ctx, int *labels, const void *a_aux);
floatval_t crf1dc_sm_viterbi(crf1d_context_t* ctx, int *labels, const void *a_aux);

void crf1dc_debug_context(FILE *fp);
void crf1dc_debug_tree_context(FILE *fp);
void crf1dc_debug_sm_context(FILE *fp);

/** @} */



/**
 * \defgroup crf1d_feature.c
 */
 /** @{ */

 /**
  * Feature type.
  */
enum {
	FT_STATE = 0,    /**< State features. */
	FT_TRANS,        /**< Transition features. */
};

/**
 * Parameters for feature generation.
 */
typedef struct {
	floatval_t  feature_minfreq; /** The threshold for occurrences of features. */
	int         feature_possible_states; /** Dense state features. */
	int         feature_possible_transitions; /** Dense transition features. */
	int         feature_max_seg_len; /** Maximum length of segments having same tag. */
	int         feature_max_order; /** Maximum order of transition features. */
} crf1de_option_t;

/**
 * A feature (for either state or transition).
 */
typedef struct {
	/**
	 * Feature type.
	 *    Possible values are:
	 *    - FT_STATE (0) for state features.
	 *    - FT_TRANS (1) for transition features.
	 */
	int        type;

	/**
	 * Source id.
	 *    The semantic of this field depends on the feature type:
	 *    - attribute id for state features (type == 0).
	 *    - output label id for transition features (type != 0).
	 */
	int        src;

	/**
	 * Destination id.
	 *    Label id emitted by this feature.
	 */
	int        dst;

	/**
	 * Frequency (observation expectation).
	 */
	floatval_t    freq;
} crf1df_feature_t;

/**
 * Feature references.
 *    This is a collection of feature ids used for faster accesses.
 */
typedef struct {
	int     num_features;    /**< Number of features referred */
	int*    fids;            /**< Array of feature ids */
} feature_refs_t;

crf1df_feature_t* crf1df_generate(int *ptr_num_features, \
	crf1de_semimarkov_t *sm, \
	int *max_items, \
	const crf1de_option_t *opt, \
	const dataset_t *ds, \
	const int ftype, \
	const int num_labels, \
	const crfsuite_logging_callback func, \
	void *instance);

int crf1df_init_references(feature_refs_t **ptr_attributes, \
	feature_refs_t **ptr_trans, \
	const crf1df_feature_t *features, \
	const crf1de_semimarkov_t *sm, \
	const int K, \
	const int A, \
	const int L);

/** @} */



/**
 * \defgroup crf1d_model.c
 */
 /** @{ */

typedef struct {
	uint8_t     magic[4];       /* File magic. */
	uint32_t    size;           /* File size. */
	uint8_t     type[4];        /* Model type */
	uint32_t    version;        /* Version number. */
	uint32_t    num_features;   /* Number of features. */
	uint32_t    num_labels;     /* Number of labels. */
	uint32_t    num_attrs;      /* Number of attributes. */
	uint32_t    off_features;   /* Offset to features. */
	uint32_t    off_labels;     /* Offset to label CQDB. */
	uint32_t    off_attrs;      /* Offset to attribute CQDB. */
	uint32_t    off_labelrefs;  /* Offset to label feature references. */
	uint32_t    off_attrrefs;   /* Offset to attribute feature references. */
	uint32_t    off_sm;	      /* Offset to semi-markov data. */
} header_t;

typedef struct {
	uint8_t     chunk[4];       /* Chunk id */
	uint32_t    size;           /* Chunk size. */
	uint32_t    num;            /* Number of items. */
	uint32_t    offsets[1];     /* Offsets. */
} featureref_header_t;

typedef struct {
	uint8_t     chunk[4];       /* Chunk id */
	uint32_t    size;           /* Chunk size. */
	uint32_t    num;            /* Number of items. */
} feature_header_t;

/* After changing size of this struct, update macro `HSM_CHUNK_SIZE` in
   `crf1d_model.c` */
typedef struct {
	uint8_t     chunk[4];       /* Chunk id */
	uint32_t    max_order;      /* Maximum order. */
	uint32_t    num_labels;     /* Number of labels. */
	uint32_t    num_states;     /* Number of states. */
	uint32_t    num_bkw_states; /* Number of transitions associated with each state. */
	uint32_t    num_suffixes;   /* Total number of suffixes. */

	uint32_t    off_max_seg_len; /* Offset of the array holding maximum segment
					lengths of the labels. */
	uint32_t    off_suffixes;	/* Offset of the array with suffixes. */
	uint32_t    off_states[1];	/* Offset of the array of states. */
} sm_header_t;

struct tag_crf1dm {
	uint8_t*    buffer_orig;
	uint8_t*    buffer;
	uint32_t    size;
	header_t*   header;
	cqdb_t*     labels;
	cqdb_t*     attrs;
	crf1de_semimarkov_t *sm;	/**< Data of semi-markov model. */
};
typedef struct tag_crf1dm crf1dm_t;

struct tag_crf1dmw {
	FILE *fp;
	int state;
	header_t header;
	cqdb_writer_t* dbw;
	featureref_header_t* href;
	feature_header_t* hfeat;
	sm_header_t* hsm;
};
typedef struct tag_crf1dmw crf1dmw_t;

typedef struct {
	int        type;
	int        src;
	int        dst;
	floatval_t weight;
} crf1dm_feature_t;

crf1dmw_t* crf1mmw(const char *filename, const int ftype);
int crf1dmw_close(crf1dmw_t* writer);
int crf1dmw_open_labels(crf1dmw_t* writer, int num_labels);
int crf1dmw_close_labels(crf1dmw_t* writer);
int crf1dmw_put_label(crf1dmw_t* writer, int lid, const char *value);
int crf1dmw_open_attrs(crf1dmw_t* writer, int num_attributes);
int crf1dmw_close_attrs(crf1dmw_t* writer);
int crf1dmw_put_attr(crf1dmw_t* writer, int aid, const char *value);
int crf1dmw_open_labelrefs(crf1dmw_t* writer, int num_labels);
int crf1dmw_close_labelrefs(crf1dmw_t* writer);
int crf1dmw_put_labelref(crf1dmw_t* writer, int lid, const feature_refs_t* ref, int *map);
int crf1dmw_open_attrrefs(crf1dmw_t* writer, int num_attrs);
int crf1dmw_close_attrrefs(crf1dmw_t* writer);
int crf1dmw_put_attrref(crf1dmw_t* writer, int aid, const feature_refs_t* ref, int *map);
int crf1dmw_open_features(crf1dmw_t* writer);
int crf1dmw_close_features(crf1dmw_t* writer);
int crf1dmw_put_feature(crf1dmw_t* writer, int fid, const crf1dm_feature_t* f);
int crf1dmw_open_sm(crf1dmw_t* writer, const crf1de_semimarkov_t* a_sm);
int crf1dmw_close_sm(crf1dmw_t* writer);
int crf1dmw_put_sm_state(crf1dmw_t* writer, int sid, const crf1de_state_t *state, \
	const crf1de_semimarkov_t* sm);

crf1dm_t* crf1dm_new(const char *filename, const int ftype);
crf1dm_t* crf1dm_new_from_memory(const void *data, size_t size, const int ftype);
void crf1dm_close(crf1dm_t* model);
int crf1dm_get_num_attrs(crf1dm_t* model);
int crf1dm_get_num_labels(crf1dm_t* model);
const char *crf1dm_to_label(crf1dm_t* model, int lid);
int crf1dm_to_lid(crf1dm_t* model, const char *value);
int crf1dm_to_aid(crf1dm_t* model, const char *value);
const char *crf1dm_to_attr(crf1dm_t* model, int aid);
int crf1dm_get_labelref(crf1dm_t* model, int lid, feature_refs_t* ref);
int crf1dm_get_attrref(crf1dm_t* model, int aid, feature_refs_t* ref);
int crf1dm_get_featureid(feature_refs_t* ref, int i);
int crf1dm_get_feature(crf1dm_t* model, int fid, crf1dm_feature_t* f);
void crf1dm_dump(crf1dm_t* model, FILE *fp);

/** @} */


#endif/*__CRF1D_H__*/
