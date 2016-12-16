all: sync labpush

sync:
	cd ../labsys
	git pull ../syscall30
	cd ../syscall30

labpush: sync
	cd ../labsys
	git push
	cd ../syscall30

pulllab:
	git pull ssh://git@scopelab.cs.nthu.edu.tw:9419/noner/syscall30.git
