common_ss.add(files('accel-common.c'))
specific_ss.add(files('accel-target.c'))
system_ss.add(files('accel-system.c', 'accel-blocker.c', 'accel-qmp.c'))
user_ss.add(files('accel-user.c'))

subdir('tcg')
if have_system
  if get_option('devproxy').enabled()
    subdir('devproxy')
  endif
  subdir('hvf')
  subdir('qtest')
  subdir('kvm')
  subdir('xen')
  subdir('stubs')
endif

# qtest
system_ss.add(files('dummy-cpus.c'))
