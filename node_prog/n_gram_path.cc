/*
 * ===============================================================
 *    Description:  N-gram path matching implementation.
 *
 *         Author:  Ted Yin, ted.sybil@gmail.com
 *
 * Copyright (C) 2015, Cornell University, see the LICENSE file
 *                     for licensing agreement
 * ===============================================================
 */

#define weaver_debug_
#include "common/stl_serialization.h"
#include "node_prog/node_prog_type.h"
#include "node_prog/n_gram_path.h"

using node_prog::search_type;
using node_prog::doc_info;
using node_prog::n_gram_path_params;
using node_prog::n_gram_path_state;
using node_prog::cache_response;
using node_prog::Cache_Value_Base;

// params
uint64_t
doc_info :: size() const
{
    return message::size(date) + message::size(pos);
}

void
doc_info :: pack(e::packer &packer) const
{
    message::pack_buffer(packer, date);
    message::pack_buffer(packer, pos);
}

void
doc_info :: unpack(e::unpacker &unpacker)
{
    message::unpack_buffer(unpacker, date);
    message::unpack_buffer(unpacker, pos);
}

n_gram_path_params :: n_gram_path_params()
    : step(0)
{
}

uint64_t
n_gram_path_params :: size() const
{
    return message::size(node_preds)
         + message::size(edge_preds)
         + message::size(doc_map)
         + message::size(coord)
         + message::size(remaining_path)
         + message::size(step)
         + message::size(unigram);
}

void
n_gram_path_params :: pack(e::packer &packer) const
{
    message::pack_buffer(packer, node_preds);
    message::pack_buffer(packer, edge_preds);
    message::pack_buffer(packer, doc_map);
    message::pack_buffer(packer, coord);
    message::pack_buffer(packer, remaining_path);
    message::pack_buffer(packer, step);
    message::pack_buffer(packer, unigram);
}

void
n_gram_path_params :: unpack(e::unpacker &unpacker)
{
    message::unpack_buffer(unpacker, node_preds);
    message::unpack_buffer(unpacker, edge_preds);
    message::unpack_buffer(unpacker, doc_map);
    message::unpack_buffer(unpacker, coord);
    message::unpack_buffer(unpacker, remaining_path);
    message::unpack_buffer(unpacker, step);
    message::unpack_buffer(unpacker, unigram);
}

n_gram_path_state :: n_gram_path_state()
{ }

uint64_t
n_gram_path_state :: size() const
{
    return 0;
}

void
n_gram_path_state :: pack(e::packer &packer) const
{
    (void)packer;
}

void
n_gram_path_state :: unpack(e::unpacker &unpacker)
{
    (void)unpacker;
}

// parse the string 'line' as a uint32_t starting at index 'idx' till the first non-digit or end of string
// store result in 'n'
// if overflow occurs or unexpected char encountered, store true in 'bad'
inline void
parse_single_uint32(const std::string &line, size_t &idx, uint32_t &n, bool &bad)
{
    uint32_t next_digit;
    static uint32_t zero = '0';
    static uint32_t max32_div10 = UINT32_MAX / 10;
    n = 0;
    while (idx < line.length()
        && (line[idx] == '0'
         || line[idx] == '1'
         || line[idx] == '2'
         || line[idx] == '3'
         || line[idx] == '4'
         || line[idx] == '5'
         || line[idx] == '6'
         || line[idx] == '7'
         || line[idx] == '8'
         || line[idx] == '9')) {
        next_digit = line[idx] - zero;
        if (next_digit > 9) { // unexpected char
            bad = true;
            WDEBUG << "Unexpected char with ascii " << (int)line[idx]
                   << " in parsing int, num currently is " << n << std::endl;
            break;
        }
        if (n > max32_div10) { // multiplication overflow
            bad = true;
            WDEBUG << "multiplication overflow" << std::endl;
            break;
        }
        n *= 10;
        if ((n + next_digit) < n) { // addition overflow
            bad = true;
            WDEBUG << "addition overflow" << std::endl;
            break;
        }
        n += next_digit;
        ++idx;
    }
}

#define TEST_BAD \
    if (bad) { \
        docs.clear(); \
        return; \
    }
void
parse_locs(const std::string &prop,
           std::unordered_map<uint32_t, doc_info> &docs)
{
    bool bad = false;
    for (size_t i = 0; i < prop.size(); i++) {
        if (prop[i] == '(') {
            doc_info cur_doc;

            // get doc id
            i++;
            uint32_t doc_id;
            parse_single_uint32(prop, i, doc_id, bad);
            TEST_BAD;
            if (prop[i] != ',') {
                docs.clear();
                return;
            }

            // get date
            i += 2;
            cur_doc.date = prop.substr(i, 4);

            // get locs
            while (prop[i] != '[') {
                i++;
            }
            i++;
            while (true) {
                uint32_t pos;
                parse_single_uint32(prop, i, pos, bad);
                TEST_BAD;
                cur_doc.pos.emplace(pos);
                if (prop[i] == ',') {
                    i += 2;
                } else if (prop[i] == ']') {
                    break;
                } else {
                    docs.clear();
                    return;
                }
            }

            docs.emplace(doc_id, cur_doc);
        }
    }
}

std::pair<search_type, std::vector<std::pair<db::remote_node, n_gram_path_params>>>
node_prog :: n_gram_path_node_program(node_prog::node &n,
   db::remote_node &rn,
   n_gram_path_params &params,
   std::function<n_gram_path_state&()> state_getter,
   std::function<void(std::shared_ptr<Cache_Value_Base>, std::shared_ptr<std::vector<db::remote_node>>, cache_key_t)>&,
   cache_response<Cache_Value_Base>*)
{
    (void)rn;
    (void)state_getter;
    /* node progs to trigger next */
    std::vector<std::pair<db::remote_node, n_gram_path_params>> next;
    node_handle_t cur_handle = n.get_handle();
    uint32_t doc_id, doc_pos;
    std::string date;

    // visit this node now
    if (!n.has_all_predicates(params.node_preds)) {
        params.doc_map.clear();
        next.emplace_back(std::make_pair(params.coord, params));
    } else {
        /* already reaches the target */
        if (params.remaining_path.empty()) {
            if (params.unigram) {
                for (edge &e: n.get_edges()) {
                    if (e.has_all_predicates(params.edge_preds)) {
                        doc_id = -1;
                        date   = "";
                        for (auto iter: e.get_properties()) {
                            if (iter[0]->key == "locs") {
                                parse_locs(iter[0]->value, params.doc_map);
                            }
                        }
                    }
                }
                next.emplace_back(std::make_pair(params.coord, params));
            } else {
                //WDEBUG << "Hit destination " << n.get_handle() << std::endl;
                next.emplace_back(std::make_pair(params.coord, params));
            }
        } else {
            db::remote_node *next_node = nullptr;
            params.step++;
            std::unordered_map<uint32_t, doc_info> new_doc_map;
            node_handle_t next_step = params.remaining_path[0];
            params.remaining_path.pop_front();

            std::unordered_set<std::string> edges_present;
            bool last_edge = false;
            for (uint16_t i = 0; i < 1000 || last_edge; i++) {
                std::string try_handle = n.get_handle() + "," + next_step + "," + std::to_string(i);
                if (n.edge_exists(try_handle)) {
                    edges_present.emplace(try_handle);
                    last_edge = true;
                } else {
                    last_edge = false;
                }
            }
            for (const std::string &eh: edges_present) {
                edge &e = n.get_edge(eh);
                if (e.has_all_predicates(params.edge_preds)) {
                    const db::remote_node &nbr = e.get_neighbor();
                    for (auto iter: e.get_properties()) {
                        if (iter[0]->key == "locs") {
                            std::string value = iter[0]->value;
                            value.erase(remove_if(value.begin(), value.end(), isspace), value.end()); // remove whitespace
                            std::stringstream ss(value);
                            std::string item;
                            while (std::getline(ss, item, ';')) {
                                item = item.substr(1, item.size()-2);

                                std::stringstream cur_ss(item);
                                std::string part;
                                std::vector<std::string> parts;
                                while (std::getline(cur_ss, part, ',')) {
                                    parts.emplace_back(part);
                                }

                                doc_id  = std::stoul(parts[0]);
                                date    = parts[1];

                                for (uint32_t i = 2; i < parts.size(); i++) {
                                    if (parts[i].back() == ']') {
                                        parts[i].pop_back();
                                    }
                                    if (parts[i].front() == '[') {
                                        parts[i] = parts[i].substr(1, parts[i].size()-1);
                                    }

                                    doc_pos = std::stoul(parts[i]);

                                    if (params.step == 1) {
                                        doc_info &doc = new_doc_map[doc_id];
                                        doc.date = date;
                                        doc.pos.emplace(doc_pos);
                                    } else {
                                        auto iter = params.doc_map.find(doc_id);
                                        if (iter != params.doc_map.end()
                                         && iter->second.pos.find(doc_pos-1) != iter->second.pos.end()) {
                                            doc_info &doc = new_doc_map[doc_id];
                                            doc.date = date;
                                            doc.pos.emplace(doc_pos);
                                        }
                                    }
                                }
                            }
                        }
                    }

                    if (!next_node) {
                        next_node = new db::remote_node(nbr);
                    }
                }
            }

            if (next_node) {
                params.doc_map = new_doc_map;
                //WDEBUG << "== progress at node " << cur_handle.c_str() << " ===\n";
                //for (const auto &ref: params.doc_map) {
                //    WDEBUG << ref.first << " " << ref.second.date << " ";
                //    for (uint32_t pos: ref.second.pos) {
                //        std::cerr << pos << " ";
                //    }
                //    std::cerr << std::endl;
                //}
                //WDEBUG << "==========================\n";
                next.emplace_back(std::make_pair(*next_node, params));
                delete next_node;
            }
            else {
                params.doc_map.clear();
                next.emplace_back(std::make_pair(params.coord, params));
            }
        }
    }

    return std::make_pair(search_type::BREADTH_FIRST, next);
}
