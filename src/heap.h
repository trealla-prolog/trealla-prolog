#pragma once

#define CALL_SKIP false
#define CALL_NOSKIP true

size_t alloc_grow(query *q, void **addr, size_t elem_size, size_t min_elements, size_t max_elements);

cell *alloc_tmp(query *q, unsigned num_cells);
cell *append_to_tmp(query *q, cell *c, pl_ctx c_ctx);
cell *clone_term_to_tmp(query *q, cell *c, pl_ctx c_ctx);
cell *copy_term_to_tmp(query *q, cell *c, pl_ctx c_ctx, bool copy_attrs);

#define get_tmp_heap_start(q) (q)->tmp_heap
#define get_tmp_heap(q,i) ((q)->tmp_heap + (i))
#define tmp_heap_used(q) (q)->tmphp

void trim_heap(query *q);
bool is_heap_since(const query *q, const frame *f, const cell *c);

cell *alloc_queuen(query *q, unsigned qnum, const cell *c);

void fix_list(cell *c);
unsigned rebase_term(query *q, cell *c, unsigned start_nbr, bool copy_attrs);

// These allocate on the heap...

cell *allocate_structure(query *q, const char *functor, const cell *c);
cell *append_structure(query *q, const cell *c);
cell *end_structure_heap(query *q);

cell *allocate_list(query *q, const cell *c);
cell *append_list(query *q, const cell *c);
cell *end_list_unsafe(query *q);
cell *end_list(query *q);

cell *init_tmp_heap(query *q);

// These are malloc'd spaces that get freed on backtracking...

cell *alloc_heap(query *q, unsigned num_cells);
cell *clone_term_to_heap(query *q, cell *c, pl_ctx c_ctx);
cell *copy_term_to_heap(query *q, cell *c, pl_ctx c_ctx, bool copy_attrs);
cell *copy_term_to_heap_with_replacement(query *q, cell *c, pl_ctx c_ctx, bool copy_attrs, cell *from, pl_ctx from_ctx, cell *to, pl_ctx to_ctx);
cell *import_term(query *q, cell *c, pl_ctx c_ctx);
void free_detached_term(cell *c);
void abandon_clone_cycles(query *q);

