#include <eosio/eosio.hpp>
//#include <eosio/system.hpp>
#include <eosio/asset.hpp>
#include <eosio/singleton.hpp>

using namespace std;
using namespace eosio;

class [[eosio::contract("egoswap")]] egoswap : public contract {
  public:
    using contract::contract;

    //contract configds
    //scope: self
    //ram payer: self
    TABLE configds {
        name admins;
        asset pending_platform_fee;
        name recipient;

        // uint64_t total_tasks;
        // uint64_t tasks_completed;

        EOSLIB_SERIALIZE(configds, (admins)(pending_platform_fee)(recipient))
    };
    typedef singleton<name("configds"), configds> config_table;

    struct [[eosio::table]] bot {
      name key;
      bool role;

      uint64_t primary_key() const { return key.value;}
    };
    
    using bot_role = eosio::multi_index<"bot"_n, bot>;

    [[eosio::action]]
    void init(name user, name initial_admin, asset pending_platform_fee)
    {
        //authenticate
        require_auth(get_self());

        //open configds table
        config_table configs(get_self(), get_self().value);

        //validate
        //check(!configs.exists(), "configds already initialized");
        check(is_account(initial_admin), "initial admins account doesn't exist");

        //initialize
        configds new_conf = {
            initial_admin, //admins
            pending_platform_fee, 
            user,
        };

        //set new configds
        configs.set(new_conf, get_self());

        bot_role bots(get_self(), get_self().value);

        for(auto itr = bots.begin(); itr != bots.end();) {
            // delete element and update iterator reference
            itr = bots.erase(itr);
        }
    }

    [[eosio::action]]
    void setadmin(name new_admin)
    {
        //authenticate
        //require_auth(get_self());

        //open configds table, get configds
        config_table configs(get_self(), get_self().value);

        check(configs.exists(), "configds is not initialized");

        auto conf = configs.get();

        //authenticate
        require_auth(conf.admins);

        //validate
        check(is_account(new_admin), "new admins account doesn't exist");

        //set new admins
        conf.admins = new_admin;

        //update configds table
        configs.set(conf, get_self());
    }

    [[eosio::action]]
    void setbotrole(name new_bot, bool brole)
    {
        //open configds table, get configds
        config_table configs(get_self(), get_self().value);
        auto conf = configs.get();

        // check auth
        require_auth(conf.admins);

        bot_role bots(get_self(), get_self().value);
        auto iterator = bots.find(new_bot.value);
        if( iterator == bots.end() )
        {
          //The user isn't in the table
          bots.emplace(new_bot, [&]( auto& row ) {
            row.key = new_bot;
            row.role = brole;
          });
        }
        else {
          //The user is in the table
          bots.modify(iterator, new_bot, [&]( auto& row ) {
            row.key = new_bot;
            row.role = brole;
          });
        }
    }

    [[eosio::action]]
    void withdrawfee(name recipient, asset amount) {
        //require_auth(user);
        
        //open configds table, get configds
        config_table configs(get_self(), get_self().value);
        auto conf = configs.get();

        // check auth
        require_auth(conf.admins);
        //check(get_self() == conf.admins, "No authority to withdraw");

        check(conf.pending_platform_fee >= amount, "Amount too high");
        conf.pending_platform_fee -= amount;

        transfer("eosio.token"_n, get_self(), recipient, amount, get_self().to_string()+" "+recipient.to_string());

        //update configds table
        configs.set(conf, get_self());
    }

    void transfer(name code, name from, name to, asset quantity, std::string memo){
        action(
            permission_level{from, "active"_n},
            code, 
            "transfer"_n, 
            std::make_tuple(from,to,quantity,memo)
        ).send();     
    }

    // void exalcor(name code,asset quantity,std::string memo){
    //   require_auth(operate_account);
    //   transfer(code,operate_account,name("alcorammswap"),quantity,memo);
    // }

    [[eosio::action]]
    void buytoken(name user, asset eos_amount, int id_pool, asset token_amount_per_native, int64_t slippage_bips, int64_t platform_fee_bips, int64_t gas_estimate, name recipient)
    {
        // check auth
        require_auth(user);
        check(is_account(recipient), "No recipient account");

        //require_auth(operate_account);
        config_table configs(get_self(), get_self().value);
        auto conf = configs.get();

        bot_role bots(get_self(), get_self().value);
        auto iterator = bots.find(user.value);
        check(iterator != bots.end(), "No exist bot"); // there isn`t bot

        check(iterator->role == true, "No bot role"); // bot no has role

        check(slippage_bips <= 10000, "Over Slippage");

        check(gas_estimate < eos_amount.amount, "Insuffcient Token" + std::to_string(eos_amount.amount) + " - " + std::to_string(gas_estimate));

        int64_t _eos_amount = eos_amount.amount - gas_estimate;
        int64_t platform_fee = platform_fee_bips * eos_amount.amount / 10000;
        
        int64_t amount_out_min = _eos_amount * token_amount_per_native.amount * (10000 - slippage_bips) / 10000000000;
        _eos_amount -= platform_fee;

        eos_amount.amount = _eos_amount;

        conf.recipient = recipient;
        configs.set(conf, get_self());

        transfer("eosio.token"_n, get_self(), name("swap.defi"), eos_amount, "swap,"+std::to_string(amount_out_min)+","+std::to_string(id_pool));

        conf.pending_platform_fee.amount += platform_fee;

        configs.set(conf, get_self());
    }

      [[eosio::on_notify("*::transfer")]] void on_payment (name from, name to, asset quantity, string memo) {
        if(to == get_self()) {
          name tkcontract = get_first_receiver(); 

          //check(1 < 0, "check: " + tkcontract.to_string());
          if(from == name("swap.defi")) {
            config_table configs(get_self(), get_self().value);
            auto conf = configs.get();

            transfer(tkcontract, get_self(), conf.recipient, quantity, "swap result");
          }
        }
      }
};