#pragma once

namespace vendor
{

struct Widget
{
	int amount;
	int penalty;
};

class Repo
{
   public:
	Widget load(int id) const;
	void audit(int id);
};

} // namespace vendor

namespace real::project
{

using AliasId = int;
using Score = int;

#define AZTECA_REAL_PROJECT_BONUS(value) ((value) + 1)

class Runner
{
   public:
	Score inspect(AliasId id);

   private:
	vendor::Repo repo_;
};

} // namespace real::project
