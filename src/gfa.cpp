#include "gfa.hpp"
#include <gfakluge.hpp>

namespace vg {

using namespace std;
using namespace gfak;

/// Represents a translation from GFA node name string to pinch thread name number.
/// Tries to translate numerical node names as themselves, to the extent possible.
class GFAToPinchTranslator {
private:
    /// Map from string name to numerical name number
    unordered_map<string, int64_t> name_to_name;
    /// Track assigned numeric names
    unordered_set<int64_t> used;
    /// What is the next unused name we can assign?
    int64_t next_unused = 1;
public:
    /// Translate from GFA name to pinch thread name
    int64_t translate(const string& name);
}

int64_t GFAToPinchTranslator::translate(const string& name) {
    // Look up the name
    auto found = name_to_name.find(name);
    
    if (found != name_to_name.end()) {
        // We have a translation already. Use it.
        return found->second;
    }
    
    // Otherwise we need to make a translation.
    // Try to find an unused number to use.
    // To start with, we have no clue what to use (0).
    int64_t assigned = 0;
    if (is_number(name)) {
        // There's a preferred number for this string. Try to use it.
        assigned = std::stol(name);
    }
    
    if (assigned <= 0 || used.count(assigned)) {
         // We need to find an unused number.
         // next_unused is always guaranteed to be unused.
         assigned = next_unused;
         next_unused++;
    }
    
    if (assigned >= next_unused) {
        // If we read in a node ID from the GFA, this can happen.
        // Budge out the assignment cursor past any numbers yet mentioned in the GFA.
        // This is guaranteed to be past the largest assigned name, and therefore unused.
        next_unused = assigned + 1;
    }
    
    // Save the assigned numeric name
    name_to_name[name] = assigned;
    // Mark it used
    used.insert(assigned);
    // Return it
    return assigned;
}

void gfa_to_graph(istream& in, VG* graph, bool only_perfect_match) {

    // Things That Are Things
    // The GFA has "sequences" and "links" and "paths"
    // Each link's CIGAR is an alignment of the start of the second sequence to the end of the first
    // The pinch graph has "threads", "adjacencies", and "blocks"
    // The vg graph has "nodes" and "edges" and "paths" (again)
    
    // We have two layers of name/ID translation
    // One is from string GFA node name to pinch thread "name" number.
    // One is from pinch block pointer to vg graph node ID
    // For a GFA with no overlap and numeric names we want them to be invisible.
    // Otherwise in some places we will have to rework things.
    

    // New algorithm
    
    // If we are doing only perfect match:
    // Make a pinch thread set
    // Every sequence goes in
    // For each link
    // Discard the link if it is not all-M in any overlap it has
    // Pinch according to the link
    // Convert the pinch blocks to graph nodes
    // Sequence is always supplied by the first block member, whatever that is
    // Links with no overlap bypass the pinch graph entirely and become edges directly by fiddly lookup
    // Then create the paths by finding the each visited GFA sequence's thread in the pinch graph and translating via the blocks to vg space
    // When overlaps happen we know the overlap length in the second sequence (which arbitrarily loses)
    // And so we know what offset in the second sequence to start at
    // And if we didn't unify anything in the pinch graph we know a vg node will start/end there
    
    // If we are allowing non-perfect matches
    // Make the sequences into pinch graph paths the same way
    // Read the links' CIGAR strings and merge based on them
    // Except don't merge mismatched bases
    // Then resolve the sequences of pinch blocks the same way (since all of a block will be identical)
    // Then for the paths, when the path goes over an overlap edge, work out what offset in the next GFA sequence it ought to start at
    // And add a mapping to that vg node
    
    // So overall the algorithm is:
    // Make a pinch thread set
    // Every sequence goes in
    // For each link
    //  If doing perfect match mode, discard if not all M
    //  Compute CIGAR length in each sequence
    //  Pinch according to the CIGAR, only merging mismatches if in perfect match mode
    // Convert the pinch blocks to graph nodes
    // Sequence is always supplied by the first block member, whatever that is
    // Links with no overlap bypass the pinch graph entirely and become edges directly by fiddly lookup
    // Then create the paths by finding the each visited GFA sequence's thread in the pinch graph and translating via the blocks to vg space
    // When overlaps happen we know the overlap length in the second sequence from the link CIGAR
    // And so we know what offset in the second sequence to start at
    // If any link was discarded, also discard the path (and warn).
    
    // So let's start
    
    // Parse the GFA
    GFAKluge gg;
    gg.parse_gfa_file(in);
    // This maps from GFA sequence name to GFA sequence record
    map<string, sequence_elem, custom_key> gfa_sequences = gg.get_name_to_seq();
    // This maps from GFA sequence name to the GFA links for which it is the source
    map<string, vector<edge_elem> > gfa_links = gg.get_seq_to_edges();
    
    // Make a pinch thread set
    auto* pinch = stPinchThreadSet_construct();

    // Make a translator to convert from GFA string names to numeric pinch thread names
    GFAToPinchTranslator gfa_to_pinch;
    
    for(auto& name_and_record : gfa_sequences) {
        // For each GFA sequence record by string name
        auto& name = name_and_record.first;
        auto& record = name_and_record.second;
        
        // Assign it a numeric pinch thread name
        auto pinch_name = gfa_to_pinch.translate(name);
        
        // Add the thread to the pinch thread set
        stPinchThreadSet_addThread(pinch, pinch_name, 0, record.sequence.size());
    }
    
    for (auto& name_and_links : gfa_links) {
        // For each set of links, by source node name
        auto& name = name_and_links.first;
        auto& links = name_and_links.second;
        
        // Find the source pinch thread
        auto source_pinch_name = gfa_to_pinch.translate(name);
        auto source_thread = stPinchThreadSet_getThread(pinch, source_pinch_name);
        
        for (auto& link : links) {
            // For each link on this source node
            
            // Get the CIGAR alignment for the link
            auto cigar = vcflib::splitCigar(link.alignment);
            
            if (only_perfect_match) {
                // We only care about all-match/mismatch CIGARs. Does this one qualify?
                bool is_all_match_mismatch = true;
                for (auto& elem : cigar) {
                    if (elem.second != "M" && elem.second != "=" && elem.second != "X") {
                        // This operation isn't any of the match/mismatch operations
                        is_all_match_mismatch = false;
                        break;
                    }
                }
                
                if (!is_all_match_mismatch) {
                    // This CIGAR has other operations in it and we want to discard the link because of it
                    continue;
                }
            }
            
            // Now we know we need to do the link and process the CIGAR.
            
            // Get the CIGAR's length in the source and the sink.
            // TODO: Is it really true that source is reference and sink is query?
            size_t source_alignment_length = 0;
            size_t sink_alignment_length = 0;
            for (auto& elem : cigar) {
                // Parse each CIGAR element
                auto& length = elem.first;
                assert(!elem.second.empty());
                switch(elem.second[0]) {
                case 'M':
                case '=':
                case 'X':
                    // Equal-length in both
                    source_alignment_length += length;
                    sink_alignment_length += length;
                    break;
                case 'I':
                    // Insert = more sink
                    sink_alignment_length += length;
                    break;
                case 'D':
                    // Deletion = more source
                    source_alignment_length += length;
                    break;
                case 'S':
                    // Soft clip = extra sink?
                    // Probably shouldn't be allowed.
                    throw runtime_error("GFA CIGAR contains a soft-clip operation; semantics unclear");
                    break;
                case 'H':
                    // Hard clip = extra sink also, but even weirder than a soft clip.
                    throw runtime_error("GFA CIGAR contains a hard-clip operation; semantics unclear");
                    break;
                default:
                    // This is an invalid operation
                    throw runtime_error("GFA CIGAR invalid: " + elem.second + " operation in " + link.alignment);
                }
            }
            
            // Work out what thread the link is to
            auto sink_pinch_name = gfa_to_pinch.translate(link.sink_name);
            auto sink_thread = stPinchThreadSet_getThread(pinch, sink_pinch_name);
            
            // Get the orientations
            bool source_backward = !link.source_orientation_forward;
            bool sink_backward = !link.sink_orientation_backward;
            
            // Get the sequence lengths
            auto source_sequence_length = stPinchThread_getLength();
            auto sink_sequence_length = stPinchThread_getLength(stPinchThreadSet_getThread(pinch, sink_pinch_name));
            
            // Set up some cursors in each node's sequence that go the right
            // direction, based on orientations. Cursors start at the first
            // base in the CIGAR, which may be past the end/before the
            // beginning on the source if the CIGAR is 0 length.
            int64_t source_cursor = source_backward ? (-1 + source_alignment_length) : (source_sequence_length - source_alignment_length);
            int64_t source_motion = source_backward ? -1 : 1;
            int64_t sink_cursor = sink_backward ? (sink_sequence_length - 1) : 0;
            int64_t sink_motion = sink_backward ? -1 : 1;
            
            // Decide if we are pinching in agreeing orientations
            bool pinch_same_strand = (source_backward == sink_backward);
            
            // Interpret the CIGAR string and perform pinches.
            
            for (auto& elem : cigar) {
                // For each cigar operation
                auto& length = elem.first;
                
                if (length == 0) {
                    // Skip 0-length operations
                    continue;
                }
                
                // Work out the sequence-local start of the region in each sequence that it may apply to, which depends on orientation.
                int64_t source_region_start = source_backward ? (source_cursor - length + 1) : source_cursor;
                int64_t sink_region_start = sink_backward ? (sink_cursor - length + 1) : sink_cursor;
                
                assert(!elem.second.empty());
                switch(elem.second[0]) {
                case 'M':
                    if (only_perfect_match) {
                        // The whole match can be merged
                        stPinchThread_pinch(source_thread, sink_thread, source_region_start, sink_region_start, length, pinch_same_strand);
                        // Advance both cursors
                        sink_cursor += sink_motion * length;
                        source_cursor += source_motion * length;
                    } else {
                        // TODO: Break up the match into = and X operations according to the sequences
                        throw runtime_error("Unimplemented");
                    }
                    break;
                case '=':
                    // Always pinch.
                    // TODO: should we check sequence equality?
                    stPinchThread_pinch(source_thread, sink_thread, source_region_start, sink_region_start, length, pinch_same_strand);
                    // Advance both cursors
                    sink_cursor += sink_motion * length;
                    source_cursor += source_motion * length;
                    break;
                case 'X':
                    // Only pinch if we are forcing matches
                    if (only_perfect_match) {
                        stPinchThread_pinch(source_thread, sink_thread, source_region_start, sink_region_start, length, pinch_same_strand);
                    }
                    // Advance both cursors
                    sink_cursor += sink_motion * length;
                    source_cursor += source_motion * length;
                    break;
                case 'I':
                    // We don't need to do any pinching, just advance the sink cursor.
                    sink_cursor += sink_motion * length;
                    break;
                case 'D':
                    // We don't need to do any pinching, just advance the source cursor.
                    source_cursor += source_motion * length;
                    break;
                default:
                    // We should ahve already checked for weird operations.
                    throw runtime_error("Invalid operation " + elem.second + " in pre-screened CIGAR");
                }
            }
            
            // If it is an M
            
            // Grab the two sequences it is trying to match
            
            // Pinch each run of equal bases
        }
    }
    
    for (edge_elem l : seq_to_edges[(it->second).name]){




    // Clean up the pinch thread set
    stPinchThreadSet_destruct(pinch);
    pinch = nullptr;





    bool reduce_overlaps = false;
    GFAKluge gg;
    gg.parse_gfa_file(in);

    map<string, sequence_elem, custom_key> name_to_seq = gg.get_name_to_seq();
    map<std::string, vector<edge_elem> > seq_to_edges = gg.get_seq_to_edges();
    map<string, sequence_elem>::iterator it;
    id_t curr_id = 1;
    map<string, id_t> id_names;
    std::function<id_t(const string&)> get_add_id = [&](const string& name) -> id_t {
        if (is_number(name)) {
            return std::stol(name);
        } else {
            auto id = id_names.find(name);
            if (id == id_names.end()) {
                id_names[name] = curr_id;
                return curr_id++;
            } else {
                return id->second;
            }
        }
    };
    for (it = name_to_seq.begin(); it != name_to_seq.end(); it++){
        auto source_id = get_add_id((it->second).name);
        //Make us some nodes
        Node n;
        n.set_sequence((it->second).sequence);
        n.set_id(source_id);
        n.set_name((it->second).name);
        graph->add_node(n);
        // Now some edges. Since they're placed in this map
        // by their from_node, it's no big deal to just iterate
        // over them.
        for (edge_elem l : seq_to_edges[(it->second).name]){
            auto sink_id = get_add_id(l.sink_name);
            Edge e;
            e.set_from(source_id);
            e.set_to(sink_id);
            e.set_from_start(!l.source_orientation_forward);
            e.set_to_end(!l.sink_orientation_forward);
            // get the cigar
            auto cigar_elems = vcflib::splitCigar(l.alignment);
            if (cigar_elems.size() == 1
                && cigar_elems.front().first > 0
                && cigar_elems.front().second == "M") {
                    reduce_overlaps = true;
                    e.set_overlap(cigar_elems.front().first);
            }
            graph->add_edge(e);
        }
        // for (path_elem p: seq_to_paths[(it->second).name]){
        //     paths.append_mapping(p.name, source_id, p.rank ,p.is_reverse);
        // }
        // remove overlapping sequences from the graph
    }
    map<string, path_elem> n_to_p = gg.get_name_to_path();
    for (auto name_path : n_to_p){
        for (int np = 0; np < name_path.second.segment_names.size(); np++){
            graph->paths.append_mapping(name_path.first, stol(name_path.second.segment_names[np]), np + 1, !name_path.second.orientations[np]);
        }
    }
    if (reduce_overlaps) {
        graph->bluntify();
    }
}

void graph_to_gfa(const VG* graph, ostream& out) {
  GFAKluge gg;
  gg.set_version(1.0);
  for (auto h : gg.get_header()){
    out << h.second.to_string();
  }

    // TODO moving to GFAKluge
    // problem: protobuf longs don't easily go to strings....
    
    graph->for_each_node([&](const Node* n) {
        sequence_elem s_elem;
        // Fill seq element for a node
        s_elem.name = to_string(n->id());
        s_elem.sequence = n->sequence();
        out << s_elem.to_string_1() << endl;
        //gg.add_sequence(s_elem);
    });
    
    auto& pathmap = graph->paths._paths;
    for (auto p : pathmap){
        path_elem p_elem;
        p_elem.name = p.first;
        for (auto m : p.second){
            p_elem.segment_names.push_back( std::to_string(m.node_id()) );
            p_elem.orientations.push_back( !m.is_reverse() );
            const Node* n = graph->get_node( m.node_id() );
            stringstream cigaro;
            //cigaro << n->sequence().size() << (p.mapping(m_ind.position().is_reverse()) ? "M" : "M");
            cigaro << n->sequence().size() << (m.is_reverse() ? "M" : "M");
            p_elem.overlaps.push_back( cigaro.str() );
        }
        out << p_elem.to_string() << endl;
        //gg.add_path(p_elem.name, p_elem);
    }

    graph->for_each_edge([&](const Edge* e) {
        edge_elem ee;
        ee.type = 1;
        ee.source_name = to_string(e->from());
        ee.sink_name = to_string(e->to());
        ee.source_orientation_forward = ! e->from_start();
        ee.sink_orientation_forward =  ! e->to_end();
        ee.alignment = std::to_string(e->overlap()) + "M";
        out << ee.to_string_1() << endl;;
        //gg.add_edge(ee.source_name, ee);
        //link_elem l;
        //l.source_name = to_string(e->from());
        //l.sink_name = to_string(e->to());
        //l.source_orientation_forward = ! e->from_start();
        //l.sink_orientation_forward =  ! e->to_end();
        //l.cigar = std::to_string(e->overlap()) + "M";
        //gg.add_link(l.source_name, l);
    });
    //gg.output_to_stream(cout);
}

}
