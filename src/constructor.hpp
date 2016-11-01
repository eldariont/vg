#ifndef VG_CONSTRUCTOR_HPP
#define VG_CONSTRUCTOR_HPP

/**
 * constructor.hpp: defines a tool class used for constructing VG graphs from
 * VCF files.
 */

#include <vector>
#include <set>
#include <map>

#include "types.hpp"

#include "vg.pb.h"

// We need vcflib
#include "Variant.h"
// And fastahack
#include "Fasta.h"

namespace vg {

using namespace std;

/**
 * Represents a constructed region of the graph alogn a single linear sequence.
 * Contains the protobuf Graph holding all the created components (which may be
 * too large to serialize), a set of node IDs whose left sides need to be
 * connected to when you connect to the start of the chunk, and a set of node
 * IDs whose right sides need to be connected to when you connect to the end of
 * the chunk.
 */
struct ConstructedChunk {
    // What nodes, edges, and mappings exist?
    Graph graph;
    
    // What nodes have left sides that match up with the left edge of the chunk?
    set<id_t> left_ends;
    // And similarly for right sides on the right edge of the chunk?
    set<id_t> right_ends;
};

/**
 * Provides a one-variant look-ahead buffer on a vcflib::VariantFile. Lets
 * construction functions peek and see if they want the next variant, or lets
 * them ignore it for the next construction function for a different contig to
 * handle. Ought not to be copied.
 *
 * Handles conversion from 1-based vcflib coordinates to 0-based vg coordinates.
 */
class VcfBuffer {

public:
    // This points to the VariantCallFile we wrap
    // We can wrap the null file (and never have any variants) with a null here.
    vcflib::VariantCallFile* file = nullptr;
    
    /**
     * Return a pointer to the buffered variant, or null if no variant is
     * buffered. Pointer is invalidated when the buffer is handled. The variant
     * will have a 0-based start coordinate.
     */
    vcflib::Variant* get();
    
    /**
     * To be called when the buffer is filled. Marks the buffered variant as
     * handled, discarding it, and allowing another to be read.
     */
    void handle_buffer();
    
    /**
     * Can be called when the buffer is filled or empty. If there is no variant
     * in the buffer, tries to load a variant into the buffer, if one can be
     * obtained from the file.
     */
    void fill_buffer();
    
protected:
    
    // This stores whether the buffer is populated with a valid variant or not
    bool has_buffer = false;
    // This is the actual buffer.
    vcflib::Variant buffer;
    
    

private:
    // Don't copy these or it will break the buffering semantics.
    VcfBuffer(const VcfBuffer& other) = delete;
    VcfBuffer& operator=(const VcfBuffer& other) = delete;
        
};

class Constructor {

public:

    // Should alts be interpreted as flat (false) or aligned back to the
    // reference by vcflib (true)?
    bool flat = false;
    
    // Should we add paths for the different alts of variants, like
    // _alt_6079b4a76d0ddd6b4b44aeb14d738509e266961c_0 and
    // _alt_6079b4a76d0ddd6b4b44aeb14d738509e266961c_1?
    bool alt_paths = false;
    
    // What's the maximum node size we should allow?
    size_t max_node_size = 1024;
    
    // How many variants do we want to put into a chunk? We'll still go over
    // this by a bit when we fetch all the overlapping variants, but this is how
    // many we shoot for.
    size_t vars_per_chunk = 1024;
    
    // How many bases do we want to have per chunk? We don't necessarily want to
    // load all of chr1 into an std::string, even if we have no variants on it.
    size_t bases_per_chunk = 1024 * 1024;
    
    // This map maps from VCF sequence names to FASTA sequence names. If a
    // VCF sequence name doesn't appear in here, it gets passed through
    // unchanged. Note that the primary path for each contig will be named after
    // the FASTA sequence name and not the VCF sequence name.
    map<string, string> vcf_renames;
    
    // This set contains the set of VCF sequence names we want to build the
    // graph for. If empty, we will build the graph for all sequences in the
    // FASTA. If nonempty, we build only for the specified sequences. If
    // vcf_renames applies a translation, these should be pre-translation, VCF-
    // namespace names.
    set<string> allowed_vcf_names;
    
    // This map maps from VCF sequence name to a (start, end) interval, in
    // 0-based end-exclusive coordinates, for the region of the sequence to
    // include in the graph. If it is set for a sequence, only that part of the
    // VCF will be used, and only that part of the primary path will be present
    // in the graph. If it is unset, the whole contig's graph will be
    // constructed. If vcf_renames applies a translation, keys should be pre-
    // translation, VCF-namespace names.
    map<string, pair<size_t, size_t>> allowed_vcf_regions;
    
    /**
     * Construct a ConstructedChunk of graph from the given piece of sequence,
     * with the given name, applying the given variants. The variants need to be
     * sorted by start position, have their start positions set to be ZERO-BASED
     * relative to the first base (0) of the given sequence, and not overlap
     * with any variants not in the vector we have (i.e. we need access to all
     * overlapping variants for this region). The variants must not extend
     * beyond the given sequence, though they can abut its edges.
     */
    ConstructedChunk construct_chunk(string reference_sequence, string reference_path_name,
        vector<vcflib::Variant> variants) const;
    
    /**
     * Construct a graph for the given VCF contig name, using the given
     * reference and the variants from the given buffered VCF file. Emits a
     * sequence of Graph chunks, which may be too big to serealize directly.
     *
     * Doesn't handle any of the setup for VCF indexing. Just scans all the
     * variants that can come out of the buffer, so make sure indexing is set on
     * the file first before passing it in.
     */
    void construct_graph(string vcf_contig, FastaReference& reference, VcfBuffer& variant_source,
        function<void(Graph&)> callback);
    
    /**
     * Construct a graph using the given FASTA references and VCFlib VCF files.
     * The VCF files are assumed to be grouped by contig and then sorted by
     * position within the contig, such that each contig is present in only one
     * file. If multiple FASTAs are used, each contig must be present in only
     * one FASTA file. Reference and VCF vectors may not contain nulls.
     */
    void construct_graph(vector<FastaReference*> references, vector<vcflib::VariantCallFile*> variant_files,
        function<void(Graph&)> callback);
    
    

};

}

#endif
