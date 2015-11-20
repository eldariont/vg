#ifndef DECON_HPP
#define DECON_HPP
#include <vector>
#include <string>
#include <iostream>
#include <unordered_map>
#include "Variant.h"
#include "index.hpp"
#include "path.hpp"
#include "vg.hpp"
#include "vg.pb.h"
#include "Fasta.h"
#include "xg.hpp"

namespace vg{
    using namespace std;
    class Deconstructor{
        public:

            Deconstructor();
            ~Deconstructor();
            void clear();
            void set_xg(string xg);
            void enumerate_path_names_in_index();
            void set_reference(string ref_file);
            void set_index(string index_file);
            void set_graph(VG* v);

            /**
             * Project a path onto another path,
             * much like a transformation of p2 onto p1 in space.
             */
            Path relative_mapping(Path& p1, Path& p2);

            /**
             * Convenience function.
             * Project a mapping with name 'p2' onto p1.
             * Return the projection as a map.
             */
            Path relative_mapping(Path& p1, string p2);

            /**
             * Build a vcf record from two paths, with the
             * second path argument taken as the reference.
             */
            vcflib::Variant path_to_variant(Path variant, Path ref);
            vcflib::Variant pathname_to_variant(string variant, Path ref);
            /**
             * Turn a vector of variants into a proper VCF.
             */
            vcflib::VariantCallFile write_variants(string filename, vector<vcflib::Variant> variants);
        private:
            // TODO Should probably be able to handle XG or VG indices
            string index_file;
            string reference;
            string xg_file;
            VG* vgraph;

    };
}
#endif
