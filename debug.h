void
printmovement()
{
	Movement *p = &mvptr;
	tracef("ptr: base=%.3g dir=%u mul=%.3g xc=%d yc=%d",
			p->basespeed, p->dir, p->mul, p->xcont, p->ycont);
	Movement *s = &mvscroll;
	tracef("scroll: base=%.3g dir=%u mul=%.3g xc=%d yc=%d",
			s->basespeed, s->dir, s->mul, s->xcont, s->ycont);
}
