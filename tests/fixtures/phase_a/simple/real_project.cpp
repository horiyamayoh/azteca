#include "real_project/api.hpp"

namespace real::project
{

Score Runner::inspect(AliasId id)
{
	auto item = repo_.load(id);
	if (item.penalty > 0)
	{
		repo_.audit(id);
	}
	return item.amount + AZTECA_REAL_PROJECT_BONUS(id) - item.penalty;
}

} // namespace real::project
