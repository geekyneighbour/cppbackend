#pragma once
#ifdef _WIN32
#include <sdkddkver.h>
#endif

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <memory>

#include "hotdog.h"
#include "result.h"

namespace net = boost::asio;
namespace sys = boost::system;

using Timer = net::steady_timer;

using HotDogHandler = std::function<void(Result<HotDog> hot_dog)>;

class Cafeteria {
public:
    explicit Cafeteria(net::io_context& io)
        : io_{ io } {
    }

    void OrderHotDog(HotDogHandler handler) {
		static std::atomic<int> next_hotdog_id{0};
        struct OrderState : std::enable_shared_from_this<OrderState> {

            net::io_context& io;
            std::shared_ptr<GasCooker> cooker;
            Store& store;
            HotDogHandler handler;

            std::shared_ptr<Bread> bread;
            std::shared_ptr<Sausage> sausage;

            Timer bread_timer;
            Timer sausage_timer;

            bool bread_ready = false;
            bool sausage_ready = false;

            OrderState(net::io_context& io,
                std::shared_ptr<GasCooker> cooker,
                Store& store,
                HotDogHandler handler)
                : io(io)
                , cooker(cooker)
                , store(store)
                , handler(std::move(handler))
                , bread(store.GetBread())
                , sausage(store.GetSausage())
                , bread_timer(io)
                , sausage_timer(io)
            {
            }

            void Start() {

                auto self = shared_from_this();

                bread->StartBake(*cooker, [self] {

                    self->bread_timer.expires_after(std::chrono::seconds(1));

                    self->bread_timer.async_wait(
                        [self](sys::error_code ec) {

                            if (ec) return;

                            self->bread->StopBaking();
                            self->bread_ready = true;

                            self->TryComplete();
                        });
                    });

                sausage->StartFry(*cooker, [self] {

                    self->sausage_timer.expires_after(std::chrono::milliseconds(1500));

                    self->sausage_timer.async_wait(
                        [self](sys::error_code ec) {

                            if (ec) return;

                            self->sausage->StopFry();
                            self->sausage_ready = true;

                            self->TryComplete();
                        });
                    });
            }

            void TryComplete() {

                if (!(bread_ready && sausage_ready))
                    return;

                try {
					static std::atomic<int> next_id{0};  // Или передавайте id из внешней функции
					int hotdog_id = next_id.fetch_add(1, std::memory_order_relaxed);

                    HotDog hotdog(
                        hotdog_id,
                        sausage,
                        bread
                    );

                    handler(Result<HotDog>(std::move(hotdog)));

                }
                catch (...) {

                    handler(Result<HotDog>::FromCurrentException());

                }
            }
        };

        auto state = std::make_shared<OrderState>(
            io_,
            gas_cooker_,
            store_,
            std::move(handler)
        );

        net::post(io_, [state] {
            state->Start();
            });
    }

private:
    net::io_context& io_;
    Store store_;
    std::shared_ptr<GasCooker> gas_cooker_ = std::make_shared<GasCooker>(io_);
};