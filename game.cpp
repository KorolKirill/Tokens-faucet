#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <eosio/system.hpp>
#include <math.h>
#include "token/token.hpp"
#include <eosio/singleton.hpp>
namespace eosio
{

    CONTRACT game : public eosio::contract
    {
    public:
        using contract::contract;

        game(eosio::name receiver, eosio::name code, eosio::datastream<const char *> ds)
            : contract(receiver, code, ds)
        {
            // constructor
        }

        [[eosio::action]] void init()
        {
            require_auth(_self);
            config.get_or_create(get_self(), config_s{});
        }

        [[eosio::action]] void createpool(
            uint32_t claim_cooldown,
            asset tokens_to_claim,
            name token_contract,
            symbol_code token_symbol_code,
            std::string ipfs_image,
            name pool_owner,
            bool whitelist)
        {
            require_auth(pool_owner);

            auto configTableItr = config.get();
            auto token_symbol = token::get_supply(token_contract, token_symbol_code).symbol;

            check(tokens_to_claim.symbol == token_symbol, "Something wrong with tokens_to_claim, symbol are not the same.");
            pools.emplace(pool_owner, [&](auto &entry)
                          {
                    entry.id = configTableItr.pool_counter;
                    entry.token_remains = asset(0, token_symbol);
                     entry.claim_cooldown = claim_cooldown;
                    entry.tokens_to_claim = tokens_to_claim;
                     entry.token_contract = token_contract;
                    entry.token_symbol = token_symbol;
                     entry.pool_owner = pool_owner;
                    entry.ipfs_image = ipfs_image;
                    entry.whitelist = whitelist; });
            
            configTableItr.pool_counter = configTableItr.pool_counter + 1;
            // increase counter of pool id
            config.set( configTableItr, get_self());
        }

        [[eosio::action]] void claimpool(name claimer, uint32_t pool_id)
        {
            require_auth(claimer);
            auto poolsTableItr = pools.require_find(pool_id, "Pool is not found");

            check(poolsTableItr->token_remains.amount > 0, "Nothing to claim");
            check(poolsTableItr->token_remains.amount >= poolsTableItr->tokens_to_claim.amount, "Nothing to claim");
            if (poolsTableItr->whitelist)
            {
                whitelistsT whitelistsTable(_self, pool_id);
                auto whitelistsTableitr = whitelistsTable.require_find(claimer.value, "You're not whitelisted.");
            }

            if (poolsTableItr->claim_cooldown != 0)
            {
                claimersT claimersTable(_self, pool_id);
                auto claimersTableItr = claimersTable.find(claimer.value);
                if (claimersTableItr == claimersTable.end())
                {
                    claimersTable.emplace(claimer, [&](auto &entry)
                                          {
                    entry.account = claimer;           
                    entry.last_claimed_time = now();
                    entry.total_claimed =  asset(0, poolsTableItr->token_symbol); });
                }
                else
                {
                    check(now() - claimersTableItr->last_claimed_time >= poolsTableItr->claim_cooldown, "Claim on cooldown.");
                    claimersTable.modify(claimersTableItr, get_self(), [&](auto &item)
                                         { item.last_claimed_time = now(); 
                                 item.total_claimed = claimersTableItr->total_claimed +poolsTableItr->tokens_to_claim; });
                }
            }

            pools.modify(poolsTableItr, get_self(), [&](auto &item)
                         { item.token_remains = poolsTableItr->token_remains - poolsTableItr->tokens_to_claim; });

            token::transfer_action transfer_a(poolsTableItr->token_contract, {_self, "active"_n});
            transfer_a.send(_self, claimer, poolsTableItr->tokens_to_claim, std::string("Claimed from 508block.dev faucet."));
        }

        [[eosio::action]] void addtowl(name authorized_account, uint32_t pool_id, std::vector<name> accounts_to_add)
        {
            require_auth(authorized_account);
            auto poolsTableItr = pools.require_find(pool_id, "Pool is not found");
            check(poolsTableItr->pool_owner == authorized_account, "You're not the owner.");
            whitelistsT whitelistsTable(_self, _self.value);
            for (int i = 0; i < accounts_to_add.size(); i++)
            {
                whitelistsTable.emplace(authorized_account, [&](auto &entry)
                                        { entry.account = accounts_to_add[i]; });
            }
        }

        [[eosio::action]] void deletepool(name authorized_account, uint32_t pool_id)
        {
            require_auth(authorized_account);
            auto poolsTableItr = pools.require_find(pool_id, "Pool is not found");
            check(has_auth(_self) || has_auth(poolsTableItr->pool_owner), "You don't have permission to delete the pool");

            // if (poolsTableItr->token_remains.amount > 0)
            // {
            //     token::transfer_action transfer_a(poolsTableItr->token_contract, {_self, "active"_n});
            //     transfer_a.send(_self, poolsTableItr->pool_owner, poolsTableItr->token_remains, std::string("Remainder from 508block.dev faucet."));
            // }
            pools.erase(poolsTableItr);
        }

        [[eosio::on_notify("*::transfer")]] void addtopool(name from, name to, asset quantity, std::string memo)
        {
            if (to == _self && memo != "")
            {
                name tkcontract = get_first_receiver();
                // look up tkcontract in your list of accepted currencies
                uint32_t pool_id = (uint32_t)stoi(memo);
                auto poolsTableItr = pools.require_find(pool_id, "Pool is not found");
                check(tkcontract == poolsTableItr->token_contract, "Wrong token contract");
                check(quantity.symbol == poolsTableItr->token_remains.symbol, "Wrong symbol");
                pools.modify(poolsTableItr, get_self(), [&](auto &item)
                             { item.token_remains = poolsTableItr->token_remains + quantity; });
            }
        }

    private:
        uint32_t now()
        {
            return current_time_point().sec_since_epoch();
        }

        struct [[eosio::table]] whitelists
        {
            name account;
            auto primary_key() const { return account.value; };
        };

        typedef eosio::multi_index<"whitelists"_n, whitelists> whitelistsT;

        struct [[eosio::table]] claimers
        {
            name account;
            uint32_t last_claimed_time;
            asset total_claimed;
            auto primary_key() const { return account.value; };
        };
        typedef eosio::multi_index<"claimers"_n, claimers> claimersT;

        struct [[eosio::table]] config_s
        {
            string version = "0.0.3";
            uint32_t pool_counter = 1;
            auto primary_key() const { return 1; };
        };

        typedef eosio::singleton<name("config"), config_s> config_t;
        // https://github.com/EOSIO/eosio.cdt/issues/280
        typedef eosio::multi_index<name("config"), config_s> config_t_for_abi;
        
        config_t config = config_t(get_self(), get_self().value);

        struct [[eosio::table]] pools_s
        {
            uint32_t id;
            asset token_remains;
            uint32_t claim_cooldown; // seconds
            name token_contract;
            symbol token_symbol;
            asset tokens_to_claim;
            name pool_owner;
            string ipfs_image;
            bool whitelist;

            auto primary_key() const { return id; };
            uint64_t get_key2() const { return token_contract.value; };
        };

        typedef eosio::multi_index<"pools"_n, pools_s, indexed_by<"tokenaddress"_n, const_mem_fun<pools_s, uint64_t, &pools_s::get_key2>>> pools_t;
        pools_t pools = pools_t(_self, _self.value);
    };
}