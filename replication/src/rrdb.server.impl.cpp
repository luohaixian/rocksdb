# include "rrdb.server.impl.h"
# include <algorithm>
# include <dsn/cpp/utils.h>
# include <rocksdb/utilities/checkpoint.h>

# ifdef __TITLE__
# undef __TITLE__
# endif
#define __TITLE__ "rrdb.server.impl"

namespace dsn {
    namespace apps {
        
        static std::string chkpt_get_dir_name(::dsn::replication::decree d, rocksdb::SequenceNumber seq)
        {
            char buffer[256];
            sprintf(buffer, "checkpoint.%lld.%lld", d, seq);
            return std::string(buffer);
        }

        static bool chkpt_init_from_dir(const char* name, ::dsn::replication::decree& d, rocksdb::SequenceNumber& seq)
        {
            return 2 == sscanf(name, "checkpoint.%lld.%lld", &d, &seq)
                && std::string(name) == chkpt_get_dir_name(d, seq);
        }

        rrdb_service_impl::rrdb_service_impl(::dsn::replication::replica* replica)
            : rrdb_service(replica), _max_checkpoint_count(3)
        {
            _is_open = false;
            _last_seq = 0;
            _last_durable_seq = 0;
            _is_catchup = false;

            // disable write ahead logging as replication handles logging instead now
            _wt_opts.disableWAL = true;
        }

        rrdb_service_impl::checkpoint_info rrdb_service_impl::parse_for_checkpoints()
        {
            std::vector<std::string> dirs;
            utils::filesystem::get_subdirectories(data_dir(), dirs, false);

            _checkpoints.clear();
            for (auto& d : dirs)
            {
                checkpoint_info ci;
                std::string d1 = d;
                d1 = d1.substr(data_dir().length() + 1);
                if (chkpt_init_from_dir(d1.c_str(), ci.d, ci.seq))
                {
                    _checkpoints.push_back(ci);
                }
                else if (d1.substr(0, 10) == std::string("checkpoint"))
                {
                    derror("invalid checkpoint directory %s, remove ...",
                        d.c_str()
                        );
                    utils::filesystem::remove_path(d);
                }
            }

            std::sort(_checkpoints.begin(), _checkpoints.end(),
                [](const checkpoint_info& l, const checkpoint_info& r) 
                { 
                    return l.d < r.d || (l.d == r.d && l.seq < r.seq);
                });

            checkpoint_info dci;
            dci.d = 0;
            dci.seq = 0;
            return _checkpoints.size() > 0 ? *_checkpoints.rbegin() : dci;
        }

        void rrdb_service_impl::write_batch()
        {
            auto opts = _wt_opts;
            opts.given_sequence_number = _last_seq + 1;
            auto status = _db->Write(opts, &_batch);
            if (!status.ok())
            {
                derror("%s failed, status = %s", __FUNCTION__, status.ToString().c_str());
                set_physical_error(status.code());
            }

            _last_seq += _batch.Count();
            dassert(_last_seq == _db->GetLatestSequenceNumber(), 
                "seq number mismatch: %lld vs %lld",
                _last_seq,
                _db->GetLatestSequenceNumber()
                );
            
            for (auto& r : _batch_repliers)
            {
                r(status.code());
            }
            
            _batch_repliers.clear();
            _batch.Clear();
        }

        void rrdb_service_impl::on_put(const update_request& update, ::dsn::rpc_replier<int>& reply)
        {
            dassert(_is_open, "rrdb service %s is not ready", data_dir().c_str());

            if (_is_catchup)
            {
                dassert(reply.is_empty(), "catchup only happens during initialization");
                if (++_last_durable_seq == _last_seq)
                    _is_catchup = false;
                else
                {
                    dbg_dassert(_last_durable_seq < _last_seq, 
                        "incorrect seq values in catch-up: %lld vs %lld",
                        _last_durable_decree,
                        _last_seq
                        );
                }

                return;
            }
            
            rocksdb::Slice skey(update.key.data(), update.key.length());
            rocksdb::Slice svalue(update.value.data(), update.value.length());
            rocksdb::Status status;

            switch (get_current_batch_state())
            {
            case BS_NOT_BATCH:
            {
                auto opts = _wt_opts;
                opts.given_sequence_number = ++_last_seq;
                status = _db->Put(opts, skey, svalue);
                if (!status.ok())
                {
                    derror("%s failed, status = %s", __FUNCTION__, status.ToString().c_str());
                    set_physical_error(status.code());
                }
                reply(status.code());
                return;
            }                

            case BS_BATCH:
                _batch.Put(skey, svalue);
                _batch_repliers.push_back(reply);
                return;

            case BS_BATCH_LAST:
                _batch.Put(skey, svalue);
                _batch_repliers.push_back(reply);

                write_batch();
                return;

            default:
                dassert(false, "invalid batch state");
            }
        }

        void rrdb_service_impl::on_remove(const ::dsn::blob& key, ::dsn::rpc_replier<int>& reply)
        {
            dassert(_is_open, "rrdb service %s is not ready", data_dir().c_str());

            if (_is_catchup)
            {
                dassert(reply.is_empty(), "catchup only happens during initialization");
                if (++_last_durable_seq == _last_seq)
                    _is_catchup = false;
                else
                {
                    dbg_dassert(_last_durable_seq < _last_seq,
                        "incorrect seq values in catch-up: %lld vs %lld",
                        _last_durable_decree,
                        _last_seq
                        );
                }

                return;
            }
            
            rocksdb::Slice skey(key.data(), key.length());
            rocksdb::Status status;

            switch (get_current_batch_state())
            {
            case BS_NOT_BATCH:
            {
                auto opts = _wt_opts;
                opts.given_sequence_number = ++_last_seq;
                status = _db->Delete(opts, skey);
                if (!status.ok())
                {
                    derror("%s failed, status = %s", __FUNCTION__, status.ToString().c_str());
                    set_physical_error(status.code());
                }
                reply(status.code());
                return;
            }

            case BS_BATCH:
                _batch.Delete(skey);
                _batch_repliers.push_back(reply);
                return;

            case BS_BATCH_LAST:
                _batch.Delete(skey);
                _batch_repliers.push_back(reply);

                write_batch();
                return;

            default:
                dassert(false, "invalid batch state");
            }
        }

        void rrdb_service_impl::on_merge(const update_request& update, ::dsn::rpc_replier<int>& reply)
        {
            dassert(_is_open, "rrdb service %s is not ready", data_dir().c_str());

            if (_is_catchup)
            {
                dassert(reply.is_empty(), "catchup only happens during initialization");
                if (++_last_durable_seq == _last_seq)
                    _is_catchup = false;
                else
                {
                    dbg_dassert(_last_durable_seq < _last_seq,
                        "incorrect seq values in catch-up: %lld vs %lld",
                        _last_durable_decree,
                        _last_seq
                        );
                }

                return;
            }
            
            rocksdb::Slice skey(update.key.data(), update.key.length());
            rocksdb::Slice svalue(update.value.data(), update.value.length());
            rocksdb::Status status;

            switch (get_current_batch_state())
            {
            case BS_NOT_BATCH:
            {
                auto opts = _wt_opts;
                opts.given_sequence_number = ++_last_seq;
                status = _db->Merge(opts, skey, svalue);
                if (!status.ok())
                {
                    derror("%s failed, status = %s", __FUNCTION__, status.ToString().c_str());
                    set_physical_error(status.code());
                }
                reply(status.code());
                return;
            }

            case BS_BATCH:
                _batch.Merge(skey, svalue);
                _batch_repliers.push_back(reply);
                return;

            case BS_BATCH_LAST:
                _batch.Merge(skey, svalue);
                _batch_repliers.push_back(reply);

                write_batch();
                return;

            default:
                dassert(false, "invalid batch state");
            }
        }

        void rrdb_service_impl::on_get(const ::dsn::blob& key, ::dsn::rpc_replier<read_response>& reply)
        {
            dassert(_is_open, "rrdb service %s is not ready", data_dir().c_str());

            read_response resp;
            rocksdb::Slice skey(key.data(), key.length());
            rocksdb::Status status = _db->Get(_rd_opts, skey, &resp.value);
            resp.error = status.code();

            reply(resp);
        }

        int  rrdb_service_impl::open(bool create_new)
        {
            dassert(!_is_open, "rrdb service %s is already opened", data_dir().c_str());

            rocksdb::Options opts;
            opts.create_if_missing = create_new;
            opts.error_if_exists = create_new;
            opts.write_buffer_size = 40 * 1024; // 40 K for testing now

            if (create_new)
            {
                auto& dir = data_dir();
                dsn::utils::filesystem::remove_path(dir);
                dsn::utils::filesystem::create_directory(dir);
            }
            else
            {
                parse_for_checkpoints();
                gc_checkpoints();
            }

            auto status = rocksdb::DB::Open(opts, data_dir() + "/rdb", &_db);
            if (status.ok())
            {
                _is_open = true;

                // load from checkpoints
                _last_durable_decree = _checkpoints.size() > 0 ? _checkpoints.rbegin()->d : 0;
                _last_durable_seq = _checkpoints.size() > 0 ? _checkpoints.rbegin()->seq : 0;
                init_last_commit_decree(_last_durable_decree.load());
             
                // however,
                // because rocksdb may persist the state on disk even without explicit flush(),
                // it is possible that the _last_seq is larger than _last_durable_seq
                // i.e., _last_durable_seq is not the ground-truth,
                // in this case, the db enters a catch-up mode, where the write ops are ignored
                // but only _last_durable_seq is increased until it equals to _last_seq.
                // 
                _last_seq = _db->GetLatestSequenceNumber();
                if (_last_seq > _last_durable_seq)
                {
                    _is_catchup = true;
                }

                ddebug("open app %s: last_committed/durable decree = <%llu, %llu>",
                    data_dir().c_str(), last_committed_decree(), last_durable_decree());
            }
            else
            {
                derror("open app %s failed, err = %s",
                    data_dir().c_str(),
                    status.ToString().c_str()
                    );
            }
            return status.code();
        }

        int  rrdb_service_impl::close(bool clear_state)
        {
            if (!_is_open)
            {
                dassert(_db == nullptr, "");
                dassert(!clear_state, ""); // should not be here if do clear
                return 0;
            }

            _is_open = false;
            delete _db;
            _db = nullptr;

            if (clear_state)
            {
                if (!dsn::utils::filesystem::remove_path(data_dir()))
                {
                    return ERR_FILE_OPERATION_FAILED;
                }
            }

            return 0;
        }

        // flush is always done in the same single thread, so
        int  rrdb_service_impl::flush(bool wait)
        {
            dassert(_is_open, "rrdb service %s is not ready", data_dir().c_str());

            if (_last_durable_decree == last_committed_decree())
                return 0;

            if (_is_catchup)
                return ERR_WRONG_TIMING;
            
            rocksdb::Checkpoint* chkpt = nullptr;
            auto status = rocksdb::Checkpoint::Create(_db, &chkpt);
            if (!status.ok())
            {
                return status.code();
            }

            checkpoint_info ci;
            ci.d = last_committed_decree();
            ci.seq = _last_seq;
            dassert(ci.seq == _db->GetLatestSequenceNumber(),
                "seq number mismatch: %lld vs %lld",
                ci.seq,
                _db->GetLatestSequenceNumber()
                );

            auto dir = chkpt_get_dir_name(ci.d, ci.seq);

            auto chkpt_dir = utils::filesystem::path_combine(data_dir(), dir);
            
            if (utils::filesystem::directory_exists(chkpt_dir))
                utils::filesystem::remove_path(chkpt_dir);

            status = chkpt->CreateCheckpoint(chkpt_dir);
            if (status.ok())
            {
                _last_durable_decree = last_committed_decree();

                {
                    utils::auto_lock<utils::ex_lock_nr> l(_checkpoints_lock);
                    _checkpoints.push_back(ci);
                }                

                gc_checkpoints();
            }
            else
            {
                derror("%s: checkpoint to %s failed, err = %s",
                    learn_dir().c_str(),
                    chkpt_dir.c_str(),
                    status.ToString().c_str()
                    );
            }

            delete chkpt;
            return status.code();
        }

        void rrdb_service_impl::gc_checkpoints()
        {
            while (true)
            {
                checkpoint_info del_d;

                {
                    utils::auto_lock<utils::ex_lock_nr> l(_checkpoints_lock);
                    if (_checkpoints.size() <= _max_checkpoint_count)
                        break;
                    del_d = *_checkpoints.begin();
                    _checkpoints.erase(_checkpoints.begin());
                }

                auto old_cpt = chkpt_get_dir_name(del_d.d, del_d.seq);
                auto old_cpt_dir = utils::filesystem::path_combine(data_dir(), old_cpt);
                if (utils::filesystem::directory_exists(old_cpt_dir))
                {
                    if (utils::filesystem::remove_path(old_cpt_dir))
                    {
                        dinfo("%s: checkpoint %s removed", data_dir().c_str(), old_cpt_dir.c_str());
                    }
                    else
                    {
                        derror("%s: remove checkpoint %s failed", data_dir().c_str(), old_cpt_dir.c_str());
                        {
                            utils::auto_lock<utils::ex_lock_nr> l(_checkpoints_lock);
                            _checkpoints.push_back(del_d);
                            std::sort(_checkpoints.begin(), _checkpoints.end(),
                                [](const checkpoint_info& l, const checkpoint_info& r)
                                {
                                    return l.d < r.d || (l.d == r.d && l.seq < r.seq);
                                }
                                );
                        }
                        break;
                    }
                }
                else
                {
                    derror("%s: checkpoint %s does not exist ...", data_dir().c_str(), old_cpt_dir.c_str());
                }
            }
        }

        void rrdb_service_impl::prepare_learning_request(/*out*/ blob& learn_req)
        {
            // nothing to do
        }

        int  rrdb_service_impl::get_learn_state(
            ::dsn::replication::decree start, 
            const blob& learn_req, 
            /*out*/ ::dsn::replication::learn_state& state)
        {
            // simply copy all files

            dassert(_is_open, "rrdb service %s is not ready", data_dir().c_str());
            checkpoint_info ci;
            ci.d = 0;
            {
                utils::auto_lock<utils::ex_lock_nr> l(_checkpoints_lock);
                if (_checkpoints.size() > 0)
                    ci = *_checkpoints.rbegin();
            }

            if (ci.d > 0)
            {
                auto dir = chkpt_get_dir_name(ci.d, ci.seq);

                auto chkpt_dir = utils::filesystem::path_combine(data_dir(), dir);
                auto succ = utils::filesystem::get_subfiles(chkpt_dir, state.files, true);
                if (!succ)
                {
                    derror("list files in chkpoint dir %s failed", chkpt_dir.c_str());
                    return ERR_FILE_OPERATION_FAILED;
                }

                // attach checkpoint info
                binary_writer writer;
                writer.write_pod(ci);
                state.meta.push_back(writer.get_buffer());
            }
            
            return 0;
        }

        int  rrdb_service_impl::apply_learn_state(::dsn::replication::learn_state& state)
        {
            int err = 0;

            if (_is_open)
            {
                // clear db
                err = close(true);
                if (err != 0)
                {
                    derror("clear db %s failed, err = %d", data_dir().c_str(), err);
                    return err;
                }
            }
            else
            {
                ::dsn::utils::filesystem::remove_path(data_dir());
            }

            // create data dir first
            ::dsn::utils::filesystem::create_directory(data_dir());

            // move learned files from learn_dir to data_dir
            std::string learn_dir = ::dsn::utils::filesystem::remove_file_name(*state.files.rbegin());
            std::string new_dir = ::dsn::utils::filesystem::path_combine(
                data_dir(),
                "rdb"
                );

            if (!::dsn::utils::filesystem::rename_path(learn_dir, new_dir))
            {
                derror("rename %s to %s failed", learn_dir.c_str(), new_dir.c_str());
                return ERR_FILE_OPERATION_FAILED;
            }

            // reopen the db with the new checkpoint files
            if (state.files.size() > 0)
                err = open(false);
            else
                err = open(true);

            if (err != 0)
            {
                derror("open db %s failed, err = %d", data_dir().c_str(), err);
                return err;
            }
            else
            {
                // load checkpoint meta info, and assign decrees
                checkpoint_info ci;

                dassert(state.meta.size() >= 1, "the learned state does not have checkpint meta info");
                binary_reader reader(state.meta[0]);
                reader.read_pod(ci);

                dassert(ci.seq == _last_seq, 
                    "seq numbers from loaded data and attached meta info do not match: %lld vs %lld",
                    ci.seq,
                    _last_seq
                    );

                init_last_commit_decree(ci.d);
                _is_catchup = false;

                // checkpoint immediately
                flush(true);
                dassert(last_durable_decree() == ci.d, 
                    "durable and commit decree mismatch after checkpoint: %lld vs %lld, mostly because the checkpointing (flush) above failed",
                    last_durable_decree(),
                    ci.d
                    );
            }

            return 0;
        }
                
        ::dsn::replication::decree rrdb_service_impl::last_durable_decree() const
        {
            return _last_durable_decree.load();
        }
    }
}
