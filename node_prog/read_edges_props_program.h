/*
 * ===============================================================
 *    Description:  Node program to read properties of edges on a single node
 *
 *        Created:  Friday 17 January 2014 11:00:03  EDT
 *
 *         Author:  Ayush Dubey, Greg Hill
 *                  dubey@cs.cornell.edu, gdh39@cornell.edu
 *
 * Copyright (C) 2013, Cornell University, see the LICENSE file
 *                     for licensing agreement
 * ================================================================
 */

#ifndef __READ_EDGES_PROPS_PROG__
#define __READ_EDGES_PROPS_PROG__

#include <vector>
#include <string>

#include "common/message.h"
#include "common/vclock.h"
#include "common/event_order.h"
#include "node.h"
#include "edge.h"
#include "db/element/remote_node.h"

namespace node_prog
{
    class read_edges_props_params : public virtual Node_Parameters_Base 
    {
        public:
            std::vector<uint64_t> edges; // empty vector means fetch props for all edges
            std::vector<std::string> keys; // empty vector means fetch all props
            std::vector<std::pair<uint64_t, std::vector<std::pair<std::string, std::string>>>> edges_props;

        public:
            virtual bool search_cache() {
                return false; // would never need to cache
            }

            virtual uint64_t cache_key() {
                return 0;
            }

            virtual uint64_t size() const 
            {
                uint64_t toRet = message::size(edges)
                    + message::size(keys)
                    + message::size(edges_props);
                return toRet;
            }

            virtual void pack(e::buffer::packer& packer) const 
            {
                message::pack_buffer(packer, edges);
                message::pack_buffer(packer, keys);
                message::pack_buffer(packer, edges_props);
            }

            virtual void unpack(e::unpacker& unpacker)
            {
                message::unpack_buffer(unpacker, edges);
                message::unpack_buffer(unpacker, keys);
                message::unpack_buffer(unpacker, edges_props);
            }
    };

    struct read_edges_props_state : public virtual Node_State_Base
    {
        virtual ~read_edges_props_state() { }

        virtual uint64_t size() const
        {
            return 0;
        }

        virtual void pack(e::buffer::packer& packer) const 
        {
            UNUSED(packer);
        }

        virtual void unpack(e::unpacker& unpacker)
        {
            UNUSED(unpacker);
        }
    };

    std::vector<std::pair<db::element::remote_node, read_edges_props_params>> 
    read_edges_props_node_program(
            node &n,
            db::element::remote_node &,
            read_edges_props_params &params,
            std::function<read_edges_props_state&()>,
            std::function<void(std::shared_ptr<node_prog::Cache_Value_Base>,
                std::shared_ptr<std::vector<db::element::remote_node>>, uint64_t)>&,
            cache_response<Cache_Value_Base>*)
    {
        for (edge &edge : n.get_edges()) {
            if (params.edges.empty() || (std::find(params.edges.begin(), params.edges.end(), edge.get_id()) != params.edges.end())) {
                std::vector<std::pair<std::string, std::string>> matching_edge_props;
                for (property &prop : edge.get_properties()) {
                    if (params.keys.empty() || (std::find(params.keys.begin(), params.keys.end(), prop.get_key()) != params.keys.end())) {
                        matching_edge_props.emplace_back(prop.get_key(), prop.get_value());
                    }
                }
                if (!matching_edge_props.empty()) {
                    params.edges_props.emplace_back(edge.get_id(), std::move(matching_edge_props));
                }
            }
        }
        return {std::make_pair(db::element::coordinator, std::move(params))}; // initializer list of vector
    }
}

#endif
