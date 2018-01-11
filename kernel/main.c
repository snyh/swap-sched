#include <linux/module.h>
#include "pg.h"

static int uicache_init(void)
{
  int ret;
  printk("uicache_init1\n");

  ret = init_hook();
  if (ret) {
    return ret;
  }

  ret = init_pg();
  if (ret) {
    return ret;
  }
  return 0;
}

static void uicache_exit(void)
{
  exit_pg();
  exit_hook();
}

module_init(uicache_init);
module_exit(uicache_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("snyh<snyh@snyh.org>");
MODULE_DESCRIPTION("Cache pages by UI events");
