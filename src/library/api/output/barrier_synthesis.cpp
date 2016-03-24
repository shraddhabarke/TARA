/*
 * Copyright 2014, IST Austria
 *
 * This file is part of TARA.
 *
 * TARA is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * TARA is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with TARA.  If not, see <http://www.gnu.org/licenses/>.
 *
 */


#include "barrier_synthesis.h"
#include "api/output/output_base_utilities.h"
#include <chrono>

using namespace tara;
using namespace tara::cssa;
using namespace tara::api::output;
using namespace tara::helpers;
using namespace std;

namespace tara {
namespace api {
namespace output {

ostream& operator<<( ostream& stream, const cycle& c ) {
  stream << c.name << "(";
  bool first = true;
  for( const auto& edge : c.edges ) {
    if(first) {
      stream << edge.before->name();
      first = false;
    }
    switch( edge.type ) {
    case edge_type::hb:  {stream << "->";} break;
    case edge_type::ppo: {stream << "=>";} break;
    case edge_type::rpo: {stream << "~>";} break;
    }
    stream << ","<< edge.after->name();
  }
  stream << ")";
  return stream;
}

// ostream& operator<<(ostream& stream, const barrier& barrier)
// {
//   // stream << barrier.name << "(";
//   // for(auto l = barrier.locations.begin(); l!=barrier.locations.end(); l++) {
//   //   stream << *l ;
//   //   if (!last_element(l, barrier.locations)) stream << ", ";
//   // }
//   // stream << ")";
//   // return stream;
// }

}}}

cycle_edge::cycle_edge( se_ptr _before, se_ptr _after, edge_type _type )
    :before(_before),after(_after),type(_type) {}

cycle::cycle( cycle& _c, unsigned i ) {
  edges = _c.edges;
  remove_prefix(i);
  assert( first() == last() );
}

void cycle::remove_prefix( unsigned i ) {
  assert( i <= edges.size() );
  edges.erase( edges.begin(), edges.begin()+i );
}

void cycle::remove_suffix( unsigned i ) {
  assert( i <= edges.size() );
  edges.erase( edges.end()-i, edges.end() );
}

void cycle::remove_prefix( se_ptr e ) {
  unsigned i = 0;
  for(; i < edges.size(); i++ ) {
    if( edges[i].before == e ) break;
  }
  remove_prefix(i);
}

void cycle::remove_suffix( se_ptr e ) {
  unsigned i = 0;
  for(; i < edges.size(); i++ ) {
    if( edges[i].after == e ) break;
  }
  remove_suffix(i+1);
}

unsigned cycle::has_cycle() {
  unsigned i = 0;
  se_ptr e = last();
  for(; i < edges.size(); i++ ) {
    if( edges[i].before == e ) break;
  }
  return i;
}

void cycle::pop() {
  edges.pop_back();
}

void cycle::close() {
  se_ptr l = last();
  remove_prefix( l );
  if( first() == l ) closed = true;
}

bool cycle::add_edge( se_ptr before, se_ptr after, edge_type t ) {
  if( !closed && last() == before ) {
    cycle_edge ed(before, after, t);
    edges.push_back(ed);
  }else{
    // thorw appriate error
    assert(false);
  }
  // close();
  return closed;
}

bool cycle::add_edge( se_ptr after, edge_type t ) {
  return add_edge( last(), after, t );
}

//----------------------------------------------------------------------------

barrier_synthesis::barrier_synthesis(bool verify, bool print_nfs)
  : print_nfs(print_nfs)
  , normal_form(false, false, false, true, verify)
{}

void barrier_synthesis::init( const hb_enc::encoding& hb_encoding,
                              const z3::solver& sol_desired,
                              const z3::solver& sol_undesired,
                              std::shared_ptr< const cssa::program > _program )
{
    output_base::init(hb_encoding, sol_desired, sol_undesired, program);
    normal_form.init(hb_encoding, sol_desired, sol_undesired, program);
    program = _program;
}


void barrier_synthesis::output(const z3::expr& output)
{
    output_base::output(output);
    normal_form.output(output);
    
    nf::result_type cnf = normal_form.get_result(false, false);

    // cycles.clear();
    // do the synthesis
    // locks.clear();
    // wait_notifies.clear();
    // barriers.clear();
    // cycle_counter = 1;
    // barrier_counter = 1;

    // measure time
    auto start_time = chrono::steady_clock::now();

    find_cycles(cnf);
    
    // Management
    // info = to_string(cycles.size()) + " " //+
    //   // to_string(barriers.size()) + " " 
    //   ;
    auto delay = chrono::steady_clock::now() - start_time;
    time = chrono::duration_cast<chrono::microseconds>(delay).count();
}

edge_type barrier_synthesis::is_ppo(se_ptr before, se_ptr after) {
  assert( before->tid == after->tid );
  unsigned b_num = before->get_instr_no();
  unsigned a_num = after->get_instr_no();
  assert( b_num <= a_num);
  if( program->is_mm_sc() )
  if( a_num == b_num ) {
    // if( before->is_rd() && after->is_wr() ) return true;
    // return false;
  }

  if( program->is_mm_sc() ) {
  }else{
  }
  return edge_type::ppo;//todo for each memory type
}

void barrier_synthesis::insert_event( vector<se_vec>& event_lists,
                                       se_ptr e ) {
  unsigned i_no = e->get_instr_no();

  se_vec& es = event_lists[e->tid];
  auto it = es.begin();
  for(; it < es.end() ;it++) {
    se_ptr& e1 = *it;
    if( e1 == e ) return;
    if( e1->get_instr_no() > i_no ||
        (e1->get_instr_no() == i_no && e1->is_rd() && e->is_rd() ) ) {
      break;
    }
  }
  it--;
  es.insert( it, e);
}

typedef  set< pair<se_ptr,se_ptr> > hb_conj;
// typedef  vector< hb_conj > se_cnf;

void barrier_synthesis::find_cycles(nf::result_type& cnf) {

  vector<vector<cycle>> all_cycles;
  all_cycles.resize( cnf.size() );
  unsigned cnf_num = 0;
  for( auto c : cnf ) {
    hb_conj hbs;
    vector<se_vec> event_lists;
    event_lists.resize( program->no_of_threads() );

    for( tara::hb_enc::hb& h : c ) {
      se_ptr b = program->se_store.at( h.loc1->name );
      se_ptr a = program->se_store.at( h.loc2->name );
      hbs.insert({b,a});
      insert_event( event_lists, b);
      insert_event( event_lists, a);
    }
    vector<cycle>& cycles = all_cycles[cnf_num];

    while( !hbs.empty() ) {
      auto hb= hbs.begin();
      se_ptr b = hb->first;
      // se_ptr a = hb->second;
      set<se_ptr> explored;
      cycle ancestor; // current candidate cycle
      std::vector< pair<se_ptr,edge_type> > stack;
      stack.push_back({b,edge_type::hb});
      while( !stack.empty() ) {
        pair<se_ptr,edge_type> pair = stack.back();
        se_ptr b = pair.first;
        edge_type type = pair.second;
        if( explored.find(b) != explored.end() ) {
          stack.pop_back();
          // explored
        }else if( ancestor.last() == b ) {
          // subtree has been explored; now I am also explored
          explored.insert(b);
          stack.pop_back();
          ancestor.pop();
        }else{
          ancestor.add_edge(b,type);
          unsigned stem_len = ancestor.has_cycle();
          if( stem_len != ancestor.size() ) {
            //cycle detected
            cycle c(ancestor, stem_len);
            cycles.push_back(c);
          }else{
            // Further expansion
            for( auto it = hbs.begin(); it != hbs.end(); it++ ) {
              auto hb_from_b = *it;
              if( hb_from_b.first == b ) {
                se_ptr a = hb_from_b.second;
                stack.push_back( {a,edge_type::hb} );
                it = hbs.erase(it);
              }
            }
            se_vec& es = event_lists[b->tid];
            auto it = es.begin();
            for(;it < es.end();it++) {
              if( *it == b ) break;
            }
            for(;it < es.end();it++) {
              se_ptr a = *it;
              if( a->tid != b->tid ) break;
              if( a->is_wr() && b->is_rd() ) break;
            }
            for(;it < es.end();it++) {
              se_ptr a = *it;
              stack.push_back( {a, is_ppo(b, a) });
            }
          }
        }
      }
    }
    cnf_num++;
  }
}


void barrier_synthesis::gather_statistics(api::metric& metric) const
{
  metric.additional_time = time;
  metric.additional_info = info;
}


//----------------------------------------------------------------------------
void barrier_synthesis::gen_max_sat_problem() {
  
}
