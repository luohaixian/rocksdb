# pragma once

# include "rrdb.server.h"
# include <rocksdb/db.h>
# include <vector>

namespace dsn {
    namespace apps {
        class rrdb_service_impl : public rrdb_service
        {
        public:
            rrdb_service_impl(::dsn::replication::replica* replica);

            virtual void on_put(const update_request& update, ::dsn::rpc_replier<int>& reply) override;
            virtual void on_remove(const ::dsn::blob& key, ::dsn::rpc_replier<int>& reply) override;
            virtual void on_merge(const update_request& update, ::dsn::rpc_replier<int>& reply) override;
            virtual void on_get(const ::dsn::blob& key, ::dsn::rpc_replier<read_response>& reply) override;

            virtual int  open(bool create_new) override;
            virtual int  close(bool clear_state) override;

            virtual int  checkpoint() override;
            virtual int  checkpoint_async() override;
            virtual int  get_checkpoint(::dsn::replication::decree start,
                    const blob& learn_req, /*out*/ ::dsn::replication::learn_state& state) override;
            virtual int  apply_checkpoint(::dsn::replication::learn_state& state, ::dsn::replication::chkpt_apply_mode mode) override;

        private:
            struct checkpoint_info
            {
                ::dsn::replication::decree d;
                rocksdb::SequenceNumber    seq;
                checkpoint_info() : d(0), seq(0) {}
                bool operator< (const checkpoint_info& o) const
                {
                    return d < o.d || (d == o.d && seq < o.seq);
                }
            };

            // parse checkpoint directories in the data dir
            checkpoint_info parse_checkpoints();
            // garbage collection checkpoints according to _max_checkpoint_count
            void gc_checkpoints();
            void write_batch();
            void check_last_seq();
            void catchup_one();

        private:
            rocksdb::DB           *_db;
            rocksdb::WriteBatch   _batch;
            std::vector<rpc_replier<int>> _batch_repliers;
            rocksdb::WriteOptions _wt_opts;
            rocksdb::ReadOptions  _rd_opts;

            volatile bool         _is_open;
            const int             _max_checkpoint_count;
            const int             _write_buffer_size;

            // ATTENTION:
            // _last_committed_decree is totally controlled by rdsn, and set to the decree of last checkpoint when open.
            // _last_durable_decree is always set to the decree of last checkpoint.
                        
            rocksdb::SequenceNumber      _last_seq; // always equal to DB::GetLatestSequenceNumber()
            volatile bool                _is_catchup;       // whether the db is in catch up mode
            rocksdb::SequenceNumber      _last_durable_seq; // valid only when _is_catchup is true
            std::vector<checkpoint_info> _checkpoints;
            ::dsn::utils::ex_lock_nr     _checkpoints_lock;
            volatile bool                _is_checkpointing; // whether the db is doing checkpoint
        };

        // --------- inline implementations -----------------
    }
}
