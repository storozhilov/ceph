// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
#include "include/rados/librados.h"
#include "include/rados/librados.hpp"
#include "test/librados/test.h"

#include <semaphore.h>
#include <errno.h>
#include <map>
#include <sstream>
#include <iostream>
#include <string>
#include <stdlib.h>
#include <unistd.h>

using namespace librados;
using ceph::buffer;
using std::map;
using std::ostringstream;
using std::string;

static sem_t sem;

bool notify_expected = false;

class WatchNotifyTestCtx : public WatchCtx
{
public:
    void notify(uint8_t opcode, uint64_t ver, bufferlist& bl)
    {
      assert(notify_expected);
      sem_post(&sem);
    }
};

int main(int args, char **argv)
{
  if (args < 3) {
    std::cerr << "Error: " << argv[0] << " pool_name obj_name" << std::endl;
    return 1;
  }

  std::string pool_name(argv[1]);
  std::string obj_name(argv[2]);
  std::cerr << "pool_name, obj_name are " << pool_name << ", " << obj_name << std::endl;

  char *id = getenv("CEPH_CLIENT_ID");
  if (id) std::cerr << "Client id is: " << id << std::endl;
  Rados cluster, ncluster;
  int ret;
  ret = cluster.init(id);
  if (ret) {
    std::cerr << "Error " << ret << " in cluster.init" << std::endl;
    return ret;
  }
  ret = cluster.conf_read_file(NULL);
  if (ret) {
    std::cerr << "Error " << ret << " in cluster.conf_read_file" << std::endl;
    return ret;
  }
  ret = cluster.conf_parse_env(NULL);
  if (ret) {
    std::cerr << "Error " << ret << " in cluster.conf_read_env" << std::endl;
    return ret;
  }
  cluster.connect();

  ret = ncluster.init(id);
  if (ret) {
    std::cerr << "Error " << ret << " in ncluster.init" << std::endl;
    return ret;
  }
  ret = ncluster.conf_read_file(NULL);
  if (ret) {
    std::cerr << "Error " << ret << " in ncluster.conf_read_file" << std::endl;
    return ret;
  }
  ret = ncluster.conf_parse_env(NULL);
  if (ret) {
    std::cerr << "Error " << ret << " in ncluster.conf_read_env" << std::endl;
    return ret;
  }
  ncluster.connect();

  // May already exist
  cluster.pool_create(pool_name.c_str());

  IoCtx ioctx, nioctx;
  cluster.ioctx_create(pool_name.c_str(), ioctx);

  ioctx.create(obj_name, false);

  // Create independent connection for notify
  ncluster.ioctx_create(pool_name.c_str(), nioctx);
  nioctx.create(obj_name, false);

  for (int i = 0; i < 100; ++i) {
    std::cerr << "Iteration " << i << std::endl;
    uint64_t handle;
    WatchNotifyTestCtx ctx;
    ret = ioctx.watch(obj_name, 0, &handle, &ctx);
    assert(!ret);
    //sleep(4);
    //system("./ceph --admin-daemon out/osd.0.asok dump_watchers");
    //system("./ceph osd blacklist ls");
    //system("./rados -p testpool listwatchers foo");

    bool do_blacklist = i % 2;
    notify_expected = true;
    if (do_blacklist) {
      cluster.test_blacklist_self(true);
      sleep(2);
      notify_expected = false;
    }
    //system("./ceph osd blacklist ls");
    bufferlist bl2;
    ret = nioctx.notify(obj_name, 0, bl2);
    assert(!ret);

    if (do_blacklist) {
      sleep(3); // Give a change to see an incorrect notify
    } else {
      TestAlarm alarm;
      sem_wait(&sem);
    }

    if (do_blacklist) {
      cluster.test_blacklist_self(false);
      sleep(2);
    }

    // This will always succeed as long as blacklist is removed
    ret = ioctx.unwatch(obj_name, handle);
    assert(!ret);

  }

  ioctx.close();
  nioctx.close();
  sem_destroy(&sem);
  return 0;
}
