# Solutions

One patch per exercise that changes files, named after the exercise's leaf folder:

```
01-arbiter.patch          03-extending-octopus/01-arbiter
02-protocol.patch         03-extending-octopus/02-protocol
03-three-levels.patch     03-extending-octopus/03-three-levels
```

Apply one with `bash tutorial/solutions/apply.sh <name>`. Exercises 00–02 change no
files and need no patch.

## For maintainers

The patches are generated from the `tutorial-solutions` branch, never edited by hand:

```shell
git diff esweek-tutorial..tutorial-solutions -- <files of that exercise> > tutorial/solutions/<name>.patch
```

CI keeps them honest: for each patch, on a clean checkout of `esweek-tutorial`,
`apply.sh` → build → that exercise's `check.sh`. A patch that stops applying or stops
passing fails the build, which is how we find out before the session rather than in
the room.

None exist yet — this directory is the contract.
