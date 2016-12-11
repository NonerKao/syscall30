all: sync labpush

sync:
	cd ../labsys
	git pull ../syscall30
	cd ../syscall30

labpush: sync
	cd ../labsys
	git push
	cd ../syscall30

