#pragma once
#include <boost/log/attributes.hpp>
#include <boost/log/expressions.hpp>
#include <boost/json.hpp>

BOOST_LOG_ATTRIBUTE_KEYWORD(additional_data, "AdditionalData", boost::json::value)
