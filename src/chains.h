// This code is part of the project "Theoretically Efficient Parallel Graph
// Algorithms Can Be Fast and Scalable", presented at Symposium on Parallelism
// in Algorithms and Architectures, 2018.
// Copyright (c) 2018 Laxman Dhulipala, Guy Blelloch, and Julian Shun
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all  copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Contains utilities for recursively peeling nodes with zero in-degree or zero
// out-degree. This code is currently not used; it was written for SCC, but we
// noticed that most of the benefit from recursively peeling come from the
// first round and dropped this pre-processing step.
//
// remove_chains(G): returns a boolean array of length n. The entry for vertex v
// is true if it is recursively removed when deleting zero in-degree or zero
// out-degree vertices.
#pragma once

#include "ligra.h"

template <class W, class L, class B>
struct Decr {
  L& cts;
  B& fl;
  Decr(L& _cts, B& _fl) : cts(_cts), fl(_fl) {}

  inline bool update(uintE s, uintE d, W w) {
    cts[d]--;
    if (cts[d] == 0) {
      fl[d] = 1;
      return true;
    }
    return false;
  }

  inline bool updateAtomic(uintE s, uintE d, W w) {
    intE ct = writeAdd(&cts[d], -1);
    if (ct == 0) {
      fl[d] = 1;
      return true;
    }
    return false;
  }

  inline bool cond(uintE d) { return !fl[d]; }
};

template <class W, class L, class B>
auto make_decr(L& cts, B& fl) {
  return Decr<W, L, B>(cts, fl);
}

template <template <class W> class vertex, class W>
auto remove_chains(graph<vertex<W>>& GA) {
  const size_t n = GA.n;
  const size_t m = GA.m;
  auto in_d =
      array_imap<intE>(n, [&](size_t i) { return GA.V[i].getInDegree(); });
  auto out_d =
      array_imap<intE>(n, [&](size_t i) { return GA.V[i].getOutDegree(); });

  auto in_v = make_in_imap<uintE>(n, [&](size_t i) { return i; });
  auto in_ends = pbbs::filter(in_v, [&](uintE v) { return in_d[v] == 0; });
  auto out_ends = pbbs::filter(in_v, [&](uintE v) { return out_d[v] == 0; });

  auto in_vs =
      (in_ends.size() > 0)
          ? vertexSubset(n, in_ends.size(),
                         ((tuple<uintE, pbbs::empty>*)in_ends.get_array()))
          : vertexSubset(n);
  auto out_vs =
      (out_ends.size() > 0)
          ? vertexSubset(n, out_ends.size(),
                         ((tuple<uintE, pbbs::empty>*)out_ends.get_array()))
          : vertexSubset(n);

  auto chains = array_imap<bool>(n, false);
  auto flags_in = array_imap<bool>(n, false);
  auto flags_out = array_imap<bool>(n, false);

  parallel_for_bc(i, 0, in_vs.size(),
                  (in_vs.size() > pbbs::kSequentialForThreshold),
                  { flags_in[in_vs.vtx(i)] = true; });

  parallel_for_bc(i, 0, out_vs.size(),
                  (out_vs.size() > pbbs::kSequentialForThreshold),
                  { flags_out[out_vs.vtx(i)] = true; });

  size_t nr = 0;
  while (in_vs.size() > 0 || out_vs.size() > 0) {
    nr++;
    cout << "ins = " << in_vs.size() << " outs = " << out_vs.size() << endl;
    if (in_vs.size() > 0) {
      in_vs.toSparse();
      parallel_for_bc(i, 0, in_vs.size(),
                      (in_vs.size() > pbbs::kSequentialForThreshold), {
                        uintE v = in_vs.vtx(i);
                        assert(flags_in[v]);
                        if (!chains[v]) {
                          chains[v] = true;
                        }
                      });
      auto next_in = edgeMap(GA, in_vs, make_decr<W>(in_d, flags_in));
      in_vs.del();
      in_vs = next_in;
    }
    if (out_vs.size() > 0) {
      out_vs.toSparse();
      parallel_for_bc(i, 0, out_vs.size(),
                      (out_vs.size() > pbbs::kSequentialForThreshold), {
                        uintE v = out_vs.vtx(i);
                        assert(flags_out[v]);
                        if (!chains[v]) {
                          chains[v] = true;
                        }
                      });
      auto next_out =
          edgeMap(GA, out_vs, make_decr<W>(out_d, flags_out), -1, in_edges);
      out_vs.del();
      out_vs = next_out;
    }
  }
  auto chain_im =
      make_in_imap<size_t>(n, [&](size_t i) { return (size_t)chains[i]; });
  cout << "total zero = " << pbbs::reduce_add(chain_im) << endl;
  cout << "nr = " << nr << endl;
  return chains;
}
