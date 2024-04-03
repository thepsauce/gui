#include "gui.h"

int BaseProc(View *view, event_t type, EventInfo *info)
{
	(void) view;
	(void) type;
	(void) info;
	return 0;
}

View base_view;

View *view_Default(void)
{
	return &base_view;
}

View *view_Create(const char *labelName, const Rect *rect)
{
	Label *label;
	Union *uni;
	View *view;

	label = environment_FindLabel(labelName);
	if (label == NULL) {
		PRINT_DEBUG();
		fprintf(stderr, "invalid label name: %s\n", labelName);
		return NULL;
	}

	uni = union_Alloc(union_Default(), sizeof(*uni));
	if (uni == NULL) {
		return NULL;
	}
	union_Init(uni, SIZE_MAX);
	view = union_Alloc(uni, sizeof(*view));
	if (view == NULL) {
		union_Free(union_Default(), uni);
		return NULL;
	}
	view->label = label;
	view->uni = uni;
	view->rect = *rect;
	if (label->numProperties != 0) {
		view->values = union_Alloc(uni, sizeof(*view->values) *
				label->numProperties);
		if (view->values == NULL) {
			union_Free(uni, view);
			union_Free(union_Default(), uni);
			return NULL;
		}
		for (Uint32 i = 0; i < label->numProperties; i++) {
			view->values[i] = label->properties[i].value;
		}
	} else {
		view->values = NULL;
	}
	view->region = NULL;
	view->prev = NULL;
	view->next = NULL;
	view->child = NULL;
	view->parent = NULL;
	label->proc(view, EVENT_CREATE, NULL);
	return view;
}

int view_SendRecursive(View *view, event_t type, EventInfo *info)
{
	for (; view != NULL; view = view->next) {
		if (view->child != NULL) {
			view_SendRecursive(view->child, type, info);
		}
		/* TODO: keep this? */
		if (view->label != NULL) {
			view->label->proc(view, type, info);
		}
	}
	return 0;
}

int view_Send(View *view, event_t type, EventInfo *info)
{
	return view->label->proc(view, type, info);
}

Value *view_GetProperty(View *view, type_t type, const char *name)
{
	Label *const label = view->label;
	for (Uint32 i = 0; i < label->numProperties; i++) {
		Property *const prop = &label->properties[i];
		if (type == prop->value.type && strcmp(prop->name, name) == 0) {
			return &view->values[i];
		}
	}
	return NULL;
}

bool view_GetBoolProperty(View *view, const char *name)
{
	Value *const prop = view_GetProperty(view, TYPE_BOOL, name);
	if (prop == NULL) {
		return false;
	}
	return prop->b;
}

int view_GetColorProperty(View *view, const char *name, rgb_t *rgb)
{
	Value *const value = view_GetProperty(view, TYPE_COLOR, name);
	if (value == NULL) {
		return -1;
	}
	*rgb = value->c;
	return 0;
}

int view_SetParent(View *view, View *parent)
{
	/* isolate the child from... */
	/* ...previous parent */
	if (view->parent != NULL && view->parent->child == view) {
		view->parent->child = view->next;
	}
	view->parent = parent;

	/* ...previous siblings */
	if (view->prev != NULL) {
		view->prev->next = view->next;
	}
	if (view->next != NULL) {
		view->next->prev = view->prev;
	}

	if (parent == NULL) {
		return 0;
	}
	if (parent->child != NULL) {
		parent->child->prev = view;
		view->next = parent->child;
		view->prev = parent->child;
	}
	parent->child = view;
	return 0;
}

void view_Delete(View *view)
{
	Union *uni;

	view_SetParent(NULL, view);
	uni = view->uni;
	union_FreeAll(uni);
	union_Free(union_Default(), uni);
}
