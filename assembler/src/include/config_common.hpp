//***************************************************************************
//* Copyright (c) 2011-2012 Saint-Petersburg Academic University
//* All Rights Reserved
//* See file LICENSE for details.
//****************************************************************************

/*
 * config_common.hpp
 *
 *  Created on: Aug 13, 2011
 *      Author: Alexey.Gurevich
 */

#pragma once

// todo: undo dirty fix
#include <boost/format.hpp>

#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/info_parser.hpp>
#include <boost/utility/enable_if.hpp>
#include <boost/type_traits.hpp>
#include <boost/foreach.hpp>
#include <boost/format.hpp>
#include <boost/algorithm/string.hpp>
#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include "simple_tools.hpp"
#include "fs_path_utils.hpp"
#include "verify.hpp"

namespace config_common {
// for enable_if/disable_if
namespace details {
template<class T, class S>
struct is_equal_type {
	static const bool value = false;
};

template<class T>
struct is_equal_type<T, T> {
	static const bool value = true;
};
}

inline void correct_relative_includes(const fs::path& p,
		fs::path const& working_dir = fs::initial_path()) {
	using namespace boost;
	using namespace boost::filesystem;

	std::ifstream f(p.string().c_str());

	vector < string > strings;

	while (f.good()) {
		string raw_line;
		getline(f, raw_line);

		strings.push_back(raw_line);
		boost::trim(raw_line);

		const string incl_prefix = "#include";

		if (starts_with(raw_line, incl_prefix)) {
			string incl_path_str = raw_line.substr(incl_prefix.size(),
					string::npos);
			trim(incl_path_str);
			trim_if(incl_path_str, is_any_of("\""));

			path incl_path = p.parent_path() / incl_path_str;
			incl_path = make_relative_path(incl_path);

			correct_relative_includes(incl_path);

			strings.back() = incl_prefix + " \"" + incl_path.string() + "\"";
		}
	}

	f.close();

	std::ofstream of(p.string().c_str());
	for (size_t i = 0; i < strings.size(); ++i)
		of << strings[i] << std::endl;
}

template<class T>
typename boost::enable_if_c<
		details::is_equal_type<T, std::string>::value
				|| boost::is_arithmetic<T>::value>::type load(T& value,
		boost::property_tree::ptree const& pt, string const& key,
		bool complete) {
	if (complete || pt.find(key) != pt.not_found())
		value = pt.get<T>(key);
}

template<class T>
typename boost::disable_if_c<
		details::is_equal_type<T, std::string>::value
				|| boost::is_arithmetic<T>::value>::type load(T& value,
		boost::property_tree::ptree const& pt, string const& key,
		bool complete) {
	if (complete || pt.find(key) != pt.not_found())
		load(value, pt.get_child(key), complete);
}

template<class T>
void load_items(std::vector<T>& vec, boost::property_tree::ptree const& pt,
		string const& key, bool complete) {
	string vector_key = key + string(".count");
	if (complete || pt.find(vector_key) != pt.not_found()) {
		size_t count = pt.get<size_t>(vector_key);

		for (size_t i = 0; i != count; ++i) {
			T t;
			load(t, pt.get_child(str(format("%s.item_%d") % key % i)),
					complete);
			vec.push_back(t);
		}
	}
}

void inline split(vector<string>& vec, string const& space_separated_list) {
	std::istringstream iss(space_separated_list);
	while (iss) {
		std::string value;
		iss >> value;
		if (value.length()) {
			vec.push_back(value);
		}
	}
}

void inline load_split(vector<string>& vec, boost::property_tree::ptree const& pt, string const& key) {
	boost::optional<string> values = pt.get_optional<string>(key);
	if (values) {
		split(vec, *values);
	}
}

template<class T>
void inline load(vector<T>& vec, boost::property_tree::ptree const& pt, string const& key, bool complete) {
	boost::optional<T> value = pt.get_optional<T>(key);
	if (value) {
		vec.push_back(*value);
		return;
	}
	for (size_t i = 0;; i++) {
		value = pt.get_optional<T>(key + "." + ToString(i));
		if (value) {
			vec.push_back(*value);
		} else if (i > 0) {
			return;
		}
	}
}

template<class T>
void load(T& value, boost::property_tree::ptree const& pt, string const& key) {
	load(value, pt, key, true);
}

template<class T>
void load(T& value, boost::property_tree::ptree const& pt, const char* key) {
	load(value, pt, string(key), true);
}

template<class T>
void load(T& value, boost::property_tree::ptree const& pt) {
	load(value, pt, true);
}

//    template<class T>
//    void load(T&, boost::property_tree::ptree const&, bool complete);

// config singleton-wrap
template<class Config>
struct config {
	//	template<typenamea...Args>
	//	static void create_instance(std::string const& filename, Args&&... args)
	//	{
	//		boost::property_tree::ptree pt;
	//		boost::property_tree::read_info(filename, pt);
	//		load(pt, inner_cfg(), std::forward<Args>(args)...);
	//	}

	static void create_instance(std::string const& filename) {
		correct_relative_includes(filename);

		boost::property_tree::ptree pt;
		boost::property_tree::read_info(filename, pt);
		load(inner_cfg(), pt);
	}

	static Config const& get() {
		return inner_cfg();
	}

	static Config& get_writable() {
		return inner_cfg();
	}

private:
	static Config& inner_cfg() {
		static Config config;
		return config;
	}
};

}

template<class T>
inline void load_param(const string& filename, const string& key,
		boost::optional<T>& value) {
	boost::property_tree::ptree pt;
	boost::property_tree::read_info(filename, pt);
	boost::optional<T> loaded_value = pt.get_optional<T>(key);
	value = loaded_value;
}

template<class T>
inline void write_param(const string& filename, const string& key,
		const boost::optional<T>& value) {
	if (value) {
		std::fstream params_stream;
		params_stream.open(filename, fstream::out | fstream::app);
		params_stream << key << "\t" << value << std::endl;
	}
}

template<class K, class V>
inline void load_param_map(const string& filename, const string& key,
		map<K, V>& value) {
	boost::property_tree::ptree pt;
	boost::property_tree::read_info(filename, pt);
	boost::optional<std::string> as_str = pt.get_optional<std::string>(key);
	if (as_str) {
		vector<std::string> key_value_pairs;
		boost::split(key_value_pairs, *as_str, boost::is_any_of(";"));
		for (auto it = key_value_pairs.begin(); it != key_value_pairs.end();
				++it) {
			vector<std::string> key_value;
			boost::split(key_value, *it, boost::is_any_of(" "));
			VERIFY(key_value.size() == 2);
			value[boost::lexical_cast<K>(key_value[0])] =
					boost::lexical_cast<K>(key_value[1]);
		}
	}
}

template<class K, class V>
inline void write_param_map(const string& filename, const string& key,
		const map<K, V>& value) {
	if (value.size() > 0) {
		std::fstream params_stream;
		params_stream.open(filename, fstream::out | fstream::app);
		params_stream << key << "\t\"";
		std::string delim = "";
		for (auto it = value.begin(); it != value.end(); ++it) {
			params_stream << delim << it->first << " " << it->second;
			delim = ";";
		}
		params_stream << "\"" << endl;
	}
}
