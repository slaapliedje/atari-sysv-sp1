/*
 * dlm_root.c -- the root mounts after the boot modules link.
 *
 * K&R C.
 */

extern void	dlm_bootlink();
extern int	__amix_vfs_mountroot();

int
vfs_mountroot(a, b, c, d)
	long a, b, c, d;
{
	dlm_bootlink();
	return __amix_vfs_mountroot(a, b, c, d);
}
