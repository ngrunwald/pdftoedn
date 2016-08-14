#include <string>
#include <iostream>
#include <fstream>
#include <clocale>

#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>

#include <poppler/GlobalParams.h>
#include <poppler/Error.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "base_types.h"
#include "pdf_error_tracker.h"
#include "pdf_reader.h"
#include "edsel_options.h"
#include "font_maps.h"
#include "util_edn.h"
#include "util_xform.h"
#include "util_versions.h"


namespace pdftoedn {

    // task-level error handler for poppler errors
    pdftoedn::ErrorTracker et;

    // run-time options passed as args
    pdftoedn::Options options;

    // process-wide font maps
    pdftoedn::DocFontMaps doc_font_maps;

} // namespace


int main(int argc, char** argv)
{
    // pass things back as utf-8
    if (!std::setlocale( LC_ALL, "" )) {
        std::cout << "Error setting locale" << std::endl;
        return -1;
    }

    // parse the options
    pdftoedn::Options::Flags flags = { false };
    std::string filename, output_file, font_map_file;
    bool show_font_list = false;
    intmax_t page_number = -1;

    try
    {
        namespace po = boost::program_options;
        po::options_description opts("Options");
        opts.add_options()
            ("output_file,o",       po::value<std::string>(&output_file)->required(),
             "REQUIRED: Destination file path to write output to.")
            ("use_page_crop_box,a", po::bool_switch(&flags.use_page_crop_box),
             "Use page crop box instead of media box when reading page content.")
            ("debug_meta,D",        po::bool_switch(&flags.include_debug_info),
             "Include additional debug metadata in output.")
            ("force_output,f"  ,    po::bool_switch(&flags.force_output_write),
             "Overwrite output file if it exists.")
            ("invisible_text,i",    po::bool_switch(&flags.include_invisible_text),
             "Include invisible text in output (for use with OCR'd documents).")
            ("links_only,l",        po::bool_switch(&flags.link_output_only),
             "Extract only link data.")
            ("font_map_file,m",     po::value<std::string>(&font_map_file),
             "JSON font mapping configuration file to use for this run.")
            ("omit_outline,O",      po::bool_switch(&flags.omit_outline),
             "Don't extract outline data.")
            ("page_number,p",       po::value<intmax_t>(&page_number),
             "Extract data for only this page.")
            ("filename",            po::value<std::string>(&filename)->required(),
             "PDF document to process.")
            ("show_font_map_list,F",po::bool_switch(&show_font_list),
             "Display the configured font substitution list and exit.")
            ("version,v",
             "Display version information and exit.")
            ("help,h",
             "Display this message.")
            ;

        po::positional_options_description p;
        p.add("filename", 1);

        po::variables_map vm;

        try
        {
            po::store(po::command_line_parser(argc, argv).options(opts).positional(p).run(), vm);

            if ( vm.count("help") ) {
                std::cout << "Usage: " << boost::filesystem::basename(argv[0]) << " [options] -o <output directory> filename" << std::endl
                          << opts << std::endl;
                return 1;
            }
            if ( vm.count("version") ) {
                std::cout << boost::filesystem::basename(argv[0]) << " "
                          << PDFTOEDN_VERSION << std::endl
                          << "Linked libraries: " << std::endl
                          << pdftoedn::util::version::info();
                return 1;
            }
            if ( vm.count("page_number")) {
                intmax_t pg = vm["page_number"].as<intmax_t>();
                if (pg < 0) {
                    std::cout << "Invalid page number " << pg << std::endl;
                    return 1;
                }
            }
            po::notify(vm);
        }
        catch (po::error& e) {
            std::cout << "Error parsing program arguments: " << e.what() << std::endl
                      << std::endl
                      << opts << std::endl;
            return 1;
        }
    }
    catch (std::exception& e)
    {
        std::cout << "Argument error: " << std::endl
                  << e.what() << std::endl;
        return 1;
    }

    //
    // try to set the options - this checks that files exist, etc.
    try
    {
        pdftoedn::options = pdftoedn::Options(filename, font_map_file, output_file, flags, (page_number >= 0 ? page_number : -1));
    }
    catch (pdftoedn::invalid_pdf& e) {
        return 5;
    }
    catch (std::exception& e) {
        return 2;
    }

    // dump the font map list and exit if the -F flag was passed
    if (show_font_list) {
        std::cout << pdftoedn::doc_font_maps << std::endl;
        return 0;
    }

    // init support libs if needed
    pdftoedn::util::xform::init_transform_lib();

    globalParams = new GlobalParams();
    globalParams->setProfileCommands(false);
    globalParams->setPrintCommands(false);

    // register the error handler for this document
    setErrorCallback(&pdftoedn::ErrorTracker::error_handler, &pdftoedn::et);

    // open the doc using arguments in Options - this step reads
    // general properties from the doc (num pages, PDF version) and
    // the outline
    pdftoedn::PDFReader doc_reader;

    // document is open and basic meta has been read. Before trying to
    // generate output, ensure the page number given is within range
    if (pdftoedn::options.page_number() > 0 &&
        pdftoedn::options.page_number() >= doc_reader.getNumPages()) {
        std::cout << "Error: requested page number " << pdftoedn::options.page_number()
                  << " is not valid. Document has "
                  << doc_reader.getNumPages() << " page"
                  << ((doc_reader.getNumPages() > 1) ? 's' : '\0')
                  << " and value must be 0-indexed."
                  << std::endl;
        delete globalParams;
        return 1;
    }

    std::ofstream output;
    output.open(pdftoedn::options.outputfile().c_str());

    if (!output.is_open()) {
        std::cout << pdftoedn::options.outputfile()
                  << "Cannot open file for write" << std::endl;
        return 1;
    }

    // write the document data
    output << doc_reader;

    // done
    output.close();
    delete globalParams;

    return 0;
}
