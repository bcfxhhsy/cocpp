#pragma once

#include "co_scheduler_factory.h"
#include "co_singleton.h"

class co_o1_scheduler_factory : public co_scheduler_factory,
                                public co_singleton<co_o1_scheduler_factory>
{
public:
    co_scheduler* create_scheduler() override;
    void          destroy_scheduler(co_scheduler* scheduler) override;
};