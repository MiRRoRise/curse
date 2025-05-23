//
// SRAVZ LLC
//

#include "parser.hpp"

parser::
parser()
{
    opt_.allow_comments = true;
    opt_.allow_trailing_commas = true;
}

boost::json::value
parser::
parse(const std::string& msg)
{
    int b = 5;
    boost::json::value val = boost::json::parse(msg, {}, opt_);
    int a = 5;
    return val.as_object();
}