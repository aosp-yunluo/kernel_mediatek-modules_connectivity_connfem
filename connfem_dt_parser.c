// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include <linux/kernel.h>
#include <linux/of_platform.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>
#include "connfem.h"

/*******************************************************************************
 *				M A C R O S
 ******************************************************************************/
#ifdef pr_fmt
#undef pr_fmt
#endif
#define pr_fmt(fmt) "["KBUILD_MODNAME"][DT]" fmt

/*******************************************************************************
 *			    D A T A   T Y P E S
 ******************************************************************************/


/*******************************************************************************
 *		    F U N C T I O N   D E C L A R A T I O N S
 ******************************************************************************/
static int cfm_dt_epaelna_parse(struct device_node *np,
				struct connfem_context *cfm);
static void cfm_dt_epaelna_free(struct cfm_dt_epaelna_context *dt,
				bool free_all);

static int cfm_dt_epaelna_hwid_parse(struct device_node *np,
				     unsigned int *hwid);

static int cfm_dt_epaelna_parts_parse(
		struct device_node *np,
		unsigned int hwid,
		struct device_node **parts_np);

static int cfm_dt_epaelna_pctl_pinmux_apply(
		struct platform_device *pdev,
		struct cfm_dt_epaelna_pctl_context *pctl);

static int cfm_dt_epaelna_pctl_state_parse(
		struct device_node *dn,
		struct connfem_epaelna_fem_info *fem_info,
		struct cfm_dt_epaelna_pctl_state_context *pstate);

static void cfm_dt_epaelna_pctl_free(struct cfm_dt_epaelna_pctl_context *pctl);

static void cfm_dt_epaelna_pctl_state_free(
		struct cfm_dt_epaelna_pctl_state_context *pstate);

static int cfm_dt_epaelna_pctl_state_find(
		const struct device_node *dn,
		char *state_name,
		unsigned int *state_index);

static bool cfm_dt_epaelna_pctl_exists(
		struct cfm_dt_epaelna_pctl_context *pctl);

static int cfm_dt_epaelna_pctl_walk(
		struct cfm_dt_epaelna_pctl_context *pctl,
		struct cfm_epaelna_pin_config *result);

static int cfm_dt_epaelna_pctl_data_parse(
		struct device_node *np,
		struct cfm_dt_epaelna_pctl_data_context *pctl_data);

static int cfm_dt_epaelna_pctl_data_mapping_parse(
		struct device_node *np,
		unsigned int *pin_cnt);

static int cfm_dt_epaelna_pctl_data_laa_pinmux_parse(
		struct device_node *np,
		unsigned int *laa_cnt);

static int cfm_dt_epaelna_flags_parse(
		struct device_node *np,
		unsigned int hwid,
		struct cfm_dt_epaelna_flags_context *flags_out);

static void cfm_dt_epaelna_flags_free(
		struct cfm_dt_epaelna_flags_context *flags);

/*******************************************************************************
 *			    P U B L I C   D A T A
 ******************************************************************************/


/*******************************************************************************
 *			   P R I V A T E   D A T A
 ******************************************************************************/


/*******************************************************************************
 *			      F U N C T I O N S
 ******************************************************************************/
int cfm_dt_parse(struct connfem_context *cfm)
{
	int err = 0;
	struct device_node *dn = cfm->pdev->dev.of_node;
	struct device_node *np = NULL;

	if (connfem_is_internal()) {
		np = of_get_child_by_name(dn, CFM_DT_NODE_EPAELNA_MTK);
		if (!np) {
			pr_info("Missing '%s', trying '%s'",
				CFM_DT_NODE_EPAELNA_MTK,
				CFM_DT_NODE_EPAELNA);
			np = of_get_child_by_name(dn, CFM_DT_NODE_EPAELNA);
		}
	} else {
		np = of_get_child_by_name(dn, CFM_DT_NODE_EPAELNA);
	}

	if (np) {
		pr_info("Selecting '%s'", np->name);
		err = cfm_dt_epaelna_parse(np, cfm);
		of_node_put(np);
		np = NULL;
	} else {
		pr_info("Missing '%s'", CFM_DT_NODE_EPAELNA);
	}

	return err;
}

void cfm_dt_free(struct cfm_dt_context *dt)
{
	cfm_dt_epaelna_free(&dt->epaelna, true);
}

static void cfm_dt_epaelna_free(struct cfm_dt_epaelna_context *dt,
				bool free_all)
{
	int i;

	if (!dt)
		return;

	/* PIN related info can always be freed without keeping */
	cfm_dt_epaelna_pctl_free(&dt->pctl);

	if (!free_all) {
		memset(&dt->pctl, 0, sizeof(dt->pctl));
		return;
	}

	/* In case we need to keep some critical information for later use,
	 * the data related to "flags" and "parts" are especially important,
	 * these should only be freed if "free_all" is specified.
	 */
	cfm_dt_epaelna_flags_free(&dt->flags);

	for (i = 0; i < CONNFEM_PORT_NUM; i++) {
		if (dt->parts_np[i]) {
			of_node_put(dt->parts_np[i]);
			dt->parts_np[i] = NULL;
		}
	}

	memset(dt, 0, sizeof(*dt));
}

static void cfm_dt_epaelna_pctl_free(struct cfm_dt_epaelna_pctl_context *pctl)
{
	if (!pctl)
		return;

	cfm_dt_epaelna_pctl_state_free(&pctl->state);
}

static void cfm_dt_epaelna_pctl_state_free(
		struct cfm_dt_epaelna_pctl_state_context *pstate)
{
	int i;

	if (!pstate)
		return;

	if (pstate->np) {
		for (i = 0; i < pstate->np_cnt; i++) {
			if (pstate->np[i]) {
				of_node_put(pstate->np[i]);
				pstate->np[i] = NULL;
			}
		}
		kfree(pstate->np);
		pstate->np = NULL;
	}

	pstate->np_cnt = 0;
}

static void cfm_dt_epaelna_flags_free(
		struct cfm_dt_epaelna_flags_context *flags)
{
	int i;

	if (!flags)
		return;

	for (i = 0; i < CONNFEM_SUBSYS_NUM; i++) {
		if (flags->np[i]) {
			of_node_put(flags->np[i]);
			flags->np[i] = NULL;
		}
	}
}

static int cfm_dt_epaelna_parse(struct device_node *np,
				struct connfem_context *cfm)
{
	int err = 0;
	struct device_node *dn = cfm->pdev->dev.of_node;
	struct cfm_dt_epaelna_context *dt = &cfm->dt.epaelna;
	struct cfm_epaelna_config *result = &cfm->epaelna;
	unsigned int hwid;

	/* Initialize */
	memset(dt, 0, sizeof(*dt));
	memset(result, 0, sizeof(*result));

	/* HWID property is optional, but must be valid if it exists */
	hwid = cfm_param_epaelna_hwid();
	if (hwid == CFM_PARAM_EPAELNA_HWID_INVALID) {
		err = cfm_dt_epaelna_hwid_parse(np, &dt->hwid);
		if (err == -ENOENT)
			dt->hwid = 0;
		else if (err < 0)
			return -EINVAL;
	} else {
		dt->hwid = hwid;
		pr_info("Force HWID: %d", dt->hwid);
	}

	/* Parse parts property */
	err = cfm_dt_epaelna_parts_parse(np, dt->hwid, dt->parts_np);
	if (err < 0) {
		err = -EINVAL;
		goto dt_epaelna_err;
	}

	/* Populate FEM info */
	err = cfm_epaelna_feminfo_populate(dt, &result->fem_info);
	if (err < 0) {
		err = -EINVAL;
		goto dt_epaelna_err;
	}

	/* Set available */
	if (result->fem_info.id)
		result->available = true;

	/* Parse subsys flags nodes */
	err = cfm_dt_epaelna_flags_parse(np, dt->hwid, &dt->flags);
	if (err < 0) {
		err = -EINVAL;
		goto dt_epaelna_err;
	}

	/* Populate subsys flags */
	err = cfm_epaelna_flags_populate(&dt->flags, result->flags_cfg);
	if (err < 0) {
		err = -EINVAL;
		goto dt_epaelna_err;
	}

	/* Parse pinctrl state property */
	err = cfm_dt_epaelna_pctl_state_parse(dn,
					      &result->fem_info,
					      &dt->pctl.state);
	if (err < 0 && err != -ENOENT) {
		err = -EINVAL;
		goto dt_epaelna_err;
	}

	/* Walk through all pinctrl nodes to parse and populate */
	if (cfm_dt_epaelna_pctl_exists(&dt->pctl)) {
		err = cfm_dt_epaelna_pctl_walk(&dt->pctl, &result->pin_cfg);
		if (err < 0) {
			err = -EINVAL;
			goto dt_epaelna_err;
		}
	} else {
		pr_info("Skip ANTSEL pin info collection");
	}

	/* Apply PINMUX only if device tree successfully parsed */
	if (cfm_dt_epaelna_pctl_exists(&dt->pctl)) {
		err = cfm_dt_epaelna_pctl_pinmux_apply(cfm->pdev, &dt->pctl);
		if (err < 0) {
			err = -EINVAL;
			goto dt_epaelna_err;
		}
	} else {
		pr_info("Skip applying GPIO PINMUX");
	}

	return 0;

dt_epaelna_err:
	cfm_dt_epaelna_free(dt, false);
	cfm_epaelna_config_free(result, false);

	return err;
}


/**
 * cfm_dt_epaelna_hwid_parse
 *	Parses and retrieves type of hardware based on hwid property.
 *
 * Parameters
 *	np	: Pointer to the node containing the 'hwid' property.
 *	hwid	: Output parameter storing the final HW ID
 *
 * Return value
 *	0	: Success, hwid will contain valid result
 *	-ENOENT	: HW ID not defined in device tree, hwid will set to 0
 *	-EINVAL : Failed to parse hwid property
 *
 */
static int cfm_dt_epaelna_hwid_parse(struct device_node *np,
				     unsigned int *hwid_out)
{
	int i, cnt, gpio_num;
	unsigned int gpio_value;
	unsigned int hwid = 0;

	/* Get number of HWID GPIO PINs if the property exists */
	cnt = of_gpio_named_count(np, CFM_DT_PROP_HWID);
	if (cnt <= 0 && cnt != -ENOENT) {
		pr_info("[WARN] Invalid '%s' property", CFM_DT_PROP_HWID);
		return -EINVAL;
	}

	pr_info("Number of HWID GPIO PINs: %d",
		(cnt == -ENOENT ? 0 : cnt));

	if (cnt <= 0)
		return -ENOENT;

	/* Retrieve hardware ID based on GPIO PIN(s) value */
	for (i = 0; i < cnt; i++) {
		/* Retrieve GPIO PIN number */
		gpio_num = of_get_named_gpio(np, CFM_DT_PROP_HWID, i);

		/* Abort if it's not valid */
		if (!gpio_is_valid(gpio_num)) {
			pr_info("[WARN] Invalid '%s' property: idx(%d)err(%d)",
				CFM_DT_PROP_HWID,
				i, gpio_num);
			hwid = 0;
			return -EINVAL;
		}

		/* Read GPIO PIN */
		gpio_value = gpio_get_value(gpio_num);
		hwid |= ((gpio_value & 0x1) << i);
		pr_info("%s[%d]: GPIO(%d)=%d, hwid(%u)",
			CFM_DT_PROP_HWID, i,
			gpio_num, gpio_value, hwid);
	}

	/* Update output parameter */
	*hwid_out = hwid;

	return 0;
}

/**
 * cfm_dt_epaelna_parts_parse
 *	Locate the correct parts based on hwid, and collect parts nodes.
 *
 * Parameters
 *	np	: Pointer to the node containing the 'parts' property.
 *	hwid	: Indication on which parts group to be parsed
 *		  Each parts group must contain CONNFEM_PORT_NUM of phandle
 *	parts_np: struct device_node *parts_np[CONNFEM_PORT_NUM]
 *		  for storing parts node pointers.
 *
 * Return value
 *	0	: Success, part & name will contain valid result
 *	-EINVAL : Error
 *
 */
static int cfm_dt_epaelna_parts_parse(
		struct device_node *np,
		unsigned int hwid,
		struct device_node **parts_np_out)
{
	int err = 0;
	int cnt, start, i;
	struct device_node *parts_np[CONNFEM_PORT_NUM];

	/* 'parts' property must exist */
	cnt = of_property_count_u32_elems(np, CFM_DT_PROP_PARTS);
	if (cnt <= 0) {
		pr_info("[WARN] Missing '%s' property", CFM_DT_PROP_PARTS);
		return -EINVAL;
	}

	/* Ensure 'parts' property is valid */
	if ((cnt % CONNFEM_PORT_NUM) != 0) {
		pr_info("[WARN] %s has %d items, must be multiple of %d",
			CFM_DT_PROP_PARTS,
			cnt, CONNFEM_PORT_NUM);
		return -EINVAL;
	}

	if (((hwid + 1) * CONNFEM_PORT_NUM) > cnt) {
		pr_info("[WARN] %s has %dx%d items, need %dx%d for hwid %d",
			CFM_DT_PROP_PARTS,
			(cnt / 2), CONNFEM_PORT_NUM,
			(hwid + 1), CONNFEM_PORT_NUM,
			hwid);
		return -EINVAL;
	}

	/* Collect node pointer to each of the required parts */
	memset(parts_np, 0, sizeof(parts_np));
	start = hwid * CONNFEM_PORT_NUM;
	for (i = 0; i < CONNFEM_PORT_NUM; i++) {
		parts_np[i] =
			of_parse_phandle(np, CFM_DT_PROP_PARTS, start + i);

		if (!parts_np[i]) {
			pr_info("[WARN] %s[%d][%d]: Invalid node at index %d",
				CFM_DT_PROP_PARTS,
				hwid, i,
				start + i);
			err = -EINVAL;
			break;
		}

		pr_info("Found %s[%d][%d]: %s",
			CFM_DT_PROP_PARTS,
			hwid, i,
			parts_np[i]->name);
	}

	/* Update DT context if parsing completes */
	if (!err) {
		memcpy(parts_np_out, parts_np, sizeof(parts_np));
		return 0;
	}

	/* Release parts node if error */
	for (i = 0; i < CONNFEM_PORT_NUM; i++) {
		if (parts_np[i])
			of_node_put(parts_np[i]);
	}

	return err;
}

/**
 * cfm_dt_epaelna_pctl_pinmux_apply
 *	Apply pinctrl pinmux
 *
 * Parameters
 *
 * Return value
 *	0	: Pinctrl pinmux successfully applied
 *	-EINVAL : Error
 *
 */
static int cfm_dt_epaelna_pctl_pinmux_apply(
		struct platform_device *pdev,
		struct cfm_dt_epaelna_pctl_context *pctl)
{
	struct pinctrl *pinctrl = NULL;
	struct pinctrl_state *pin_state = NULL;
	int err;

	pinctrl = devm_pinctrl_get(&pdev->dev);
	if (IS_ERR(pinctrl)) {
		pr_info("Unable to get pinctrl: %ld", PTR_ERR(pinctrl));
		return -EINVAL;
	}

	pin_state = pinctrl_lookup_state(pinctrl, pctl->state.name);
	if (IS_ERR(pin_state)) {
		pr_info("Unable to find state '%s': %ld",
			pctl->state.name,
			PTR_ERR(pin_state));

		devm_pinctrl_put(pinctrl);
		return -EINVAL;
	}

	err = pinctrl_select_state(pinctrl, pin_state);
	if (err < 0) {
		pr_info("Failed to apply pinctrl for state '%s', err %d",
			pctl->state.name, err);
		err = -EINVAL;
	} else {
		pr_info("'%s' pinmux successfully applied",
			pctl->state.name);
		err = 0;
	}

	devm_pinctrl_put(pinctrl);
	return err;
}

/**
 * cfm_dt_epaelna_pctl_state_parse
 *	Collect pinctrl nodes associated to the FEM Info.
 *
 * Parameters
 *	np	: Pointer to the node containing the 'pinctrl-names' property.
 *
 * Return value
 *	0	: Success, part & name will contain valid result
 *	-ENOENT : State could not be found
 *	-EINVAL : Error
 *
 */
static int cfm_dt_epaelna_pctl_state_parse(
		struct device_node *dn,
		struct connfem_epaelna_fem_info *fem_info,
		struct cfm_dt_epaelna_pctl_state_context *pstate)
{
	int err = 0;
	int i;

	/* Locate pinctrl state index */
	snprintf(pstate->name, sizeof(pstate->name),
		 "%s_%s",
		 fem_info->part_name[CONNFEM_PORT_WFG],
		 fem_info->part_name[CONNFEM_PORT_WFA]);

	err = cfm_dt_epaelna_pctl_state_find(dn,
					     pstate->name,
					     &pstate->index);
	if (err < 0)
		return err;	/* -ENOENT, -EINVAL */

	/* Collect pinctrl nodes */
	snprintf(pstate->prop_name, sizeof(pstate->prop_name),
		 "%s%d",
		 CFM_DT_PROP_PINCTRL_PREFIX, pstate->index);

	err = of_property_count_u32_elems(dn, pstate->prop_name);
	if (err <= 0) {
		pr_info("[WARN] '%s' is at %d, invalid '%s' prop, err %d",
			pstate->name,
			pstate->index,
			pstate->prop_name,
			err);
		return -EINVAL;
	}
	pstate->np_cnt = (unsigned int)err;

	pr_info("Found pinctrl state '%s', prop '%s', with %d nodes",
		pstate->name, pstate->prop_name, pstate->np_cnt);

	pstate->np = kcalloc(pstate->np_cnt,
			     sizeof(*(pstate->np)),
			     GFP_KERNEL);

	err = 0;
	for (i = 0; i < pstate->np_cnt; i++) {
		pstate->np[i] = of_parse_phandle(dn, pstate->prop_name, i);
		if (!pstate->np[i]) {
			pr_info("[WARN] Unable to get pinctrl node %s[%d]",
				pstate->prop_name, i);
			err = -EINVAL;
			break;
		}
		/* pr_info("Collected [%d]'%s'", i, pstate->np[i]->name); */
	}

	/* Release the partially collected pinctrl nodes in case of error */
	if (err < 0)
		cfm_dt_epaelna_pctl_state_free(pstate);
	return err;
}

/**
 * cfm_dt_epaelna_pctl_state_find
 *	Find the pinctrl state's index.
 *
 * Parameters
 *	np	   : Pointer to the node containing 'pinctrl-names' prop.
 *	state_name : Pinctrl state name to be located.
 *	state_index: Output parameter storing the index found
 *
 * Return value
 *	0	: Success, state_index will contain valid result
 *	-ENOENT : State could not be found
 *	-EINVAL : Error
 *
 */
static int cfm_dt_epaelna_pctl_state_find(
		const struct device_node *dn,
		char *state_name,
		unsigned int *state_index)
{
	struct property *p;
	const char *name = NULL;
	int i = 0;

	of_property_for_each_string(dn, "pinctrl-names", p, name) {
		if (strncmp(name, state_name,
			    CFM_DT_PCTL_STATE_NAME_SIZE) == 0) {
			break;
		}
		i++;
	}

	if (!name) {
		pr_info("[WARN] pinctrl-names does not have '%s' state",
			state_name);
		return -ENOENT;
	}

	*state_index = i;
	return 0;
}

/**
 * cfm_dt_epaelna_pctl_exists
 *	Returns true/false indicating if pinctrl nodes exist
 *
 * Parameters
 *
 * Return value
 *
 */
static bool cfm_dt_epaelna_pctl_exists(
		struct cfm_dt_epaelna_pctl_context *pctl)
{
	return pctl && (pctl->state.np_cnt > 0);
}

/**
 * cfm_dt_epaelna_pctl_walk
 *	Walk through pinctrl nodes and populate pin config result
 *
 * Parameters
 *
 * Return value
 *	0	: Success, pin config output will be valid
 *	-EINVAL : Error
 *
 */
static int cfm_dt_epaelna_pctl_walk(
		struct cfm_dt_epaelna_pctl_context *pctl,
		struct cfm_epaelna_pin_config *result_out)
{
	int err = 0;
	int i;
	struct device_node *np;
	struct cfm_dt_epaelna_pctl_data_context pctl_data;
	struct cfm_epaelna_pin_config result;

	memset(&result, 0, sizeof(result));

	for (i = 0; i < pctl->state.np_cnt; i++) {
		for_each_child_of_node(pctl->state.np[i], np) {
			pr_info("Checking [%d]%s.%s",
				i, pctl->state.np[i]->name, np->name);

			/* Aligned with Kernel pinctrl's expectation on the
			 * existence of 'pinmux' property, this check prevents
			 * us from mis-collecting the PIN mapping and the real
			 * pinmux setting.
			 *
			 * Kernel pinctrl in fact will log runtime error, and
			 * our probe callback does not get called in this case.
			 *
			 * So we probably wont need this check, but leave it
			 * there in case Kernel behaviour changes in future.
			 *
			 */
			if (!of_get_property(np, CFM_DT_PROP_PINMUX, NULL)) {
				pr_info("Missing '%s' property",
					CFM_DT_PROP_PINMUX);
				of_node_put(np);
				continue;
			}

			err = cfm_dt_epaelna_pctl_data_parse(np, &pctl_data);
			if (err < 0)
				break;

			/* As we're iterating through each of the pinctrl
			 * nodes, the previous result has to be kept, and
			 * new info should be appended instead.
			 */
			err = cfm_epaelna_pincfg_populate(&pctl_data, &result);
			if (err < 0)
				break;

			of_node_put(np);
		}
		if (err < 0)
			break;
	}

	if (err < 0) {
		pr_info("[WARN] Error while parsing [%d]%s.%s",
			i, pctl->state.np[i]->name,
			(np ? np->name : "(null)"));
		of_node_put(np);
	} else {
		memcpy(result_out, &result, sizeof(result));

		cfm_epaelna_pininfo_dump(&result_out->pin_info);
		cfm_epaelna_laainfo_dump(&result_out->laa_pin_info);

	}

	return err;
}

/**
 * cfm_dt_epaelna_pctl_data_parse
 *	Parses the required properties in the pinctrl data node
 *
 * Parameters
 *	np: Pointer to the node containing the 'pinmux' property.
 *
 * Return value
 *	0	: Success, pin config output will be valid
 *	-EINVAL : Error
 *
 */
static int cfm_dt_epaelna_pctl_data_parse(
		struct device_node *np,
		struct cfm_dt_epaelna_pctl_data_context *pctl_data_out)
{
	int err = 0;
	struct cfm_dt_epaelna_pctl_data_context pctl_data;

	memset(&pctl_data, 0, sizeof(pctl_data));
	pctl_data.np = np;

	err = cfm_dt_epaelna_pctl_data_mapping_parse(np, &pctl_data.pin_cnt);
	if (err < 0)
		return err;

	err = cfm_dt_epaelna_pctl_data_laa_pinmux_parse(np,
							 &pctl_data.laa_cnt);
	if (err < 0)
		return err;

	memcpy(pctl_data_out, &pctl_data,
	       sizeof(pctl_data));

	return 0;
}

/**
 * cfm_dt_epaelna_pctl_data_mapping_parse
 *	Parses the 'mapping' property in the pinctrl data node
 *
 * Parameters
 *	np: Pointer to the node containing the 'mapping' property.
 *
 * Return value
 *	0	: Success, pin config output will be valid
 *	-EINVAL : Error
 *
 */
static int cfm_dt_epaelna_pctl_data_mapping_parse(
		struct device_node *np,
		unsigned int *pin_cnt_out)
{
	int err = 0;
	unsigned int pin_cnt;
	unsigned int mappings;

	/* Get number of entries in pinmux and mapping */
	err = of_property_count_u32_elems(np, CFM_DT_PROP_PINMUX);
	if (err <= 0) {
		pr_info("[WARN] Invalid or missing '%s' property, err %d",
			CFM_DT_PROP_PINMUX, err);
		return -EINVAL;
	}
	pin_cnt = (unsigned int)err;
	err = 0;

	err = of_property_count_u32_elems(np, CFM_DT_PROP_MAPPING);
	if (err <= 0) {
		pr_info("[WARN] Invalid or missing '%s' property, err %d",
			CFM_DT_PROP_MAPPING, err);
		return -EINVAL;
	}
	mappings = (unsigned int)err;
	err = 0;

	/* Validate the number of entries in pinmux and mapping */
	if (pin_cnt > CONNFEM_EPAELNA_PIN_COUNT) {
		pr_info("[WARN] '%s' exceeds limit of %d PINs, currently %d",
			CFM_DT_PROP_PINMUX,
			CONNFEM_EPAELNA_PIN_COUNT,
			pin_cnt);
		return -EINVAL;
	}

	if ((mappings % CFM_DT_MAPPING_SIZE) != 0) {
		pr_info("[WARN] '%s' needs to be multiple of %d, currently %d",
			CFM_DT_PROP_MAPPING, CFM_DT_MAPPING_SIZE, mappings);
		return -EINVAL;
	}

	mappings /= CFM_DT_MAPPING_SIZE;

	if (pin_cnt != mappings) {
		pr_info("[WARN] Unequal number of entries: '%s':%d,'%s':%d",
			CFM_DT_PROP_PINMUX, pin_cnt,
			CFM_DT_PROP_MAPPING, mappings);
		return -EINVAL;
	}

	*pin_cnt_out = pin_cnt;

	return 0;
}

/**
 * cfm_dt_epaelna_pctl_data_laa_pinmux_parse
 *	Parses the 'laa-mapping' property in the pinctrl data node
 *
 * Parameters
 *	np: Pointer to the node containing the 'mapping' property.
 *
 * Return value
 *	0	: Success, pin config output will be valid
 *	-EINVAL : Error
 *
 */
static int cfm_dt_epaelna_pctl_data_laa_pinmux_parse(
		struct device_node *np,
		unsigned int *laa_cnt_out)
{
	int err = 0;
	unsigned int laa_cnt;

	/* laa-pinmux property is optional */
	err = of_property_count_u32_elems(np, CFM_DT_PROP_LAA_PINMUX);
	if (err <= 0)
		laa_cnt = 0;
	else
		laa_cnt = (unsigned int)err;

	/* Validate the number of entries in laa-pinmux */
	if ((laa_cnt % CFM_DT_LAA_PINMUX_SIZE) != 0) {
		pr_info("[WARN] '%s' needs to be multiple of %d, currently %d",
			CFM_DT_PROP_LAA_PINMUX,
			CFM_DT_LAA_PINMUX_SIZE,
			laa_cnt);
		return -EINVAL;
	}

	laa_cnt /= CFM_DT_LAA_PINMUX_SIZE;

	if (laa_cnt > CONNFEM_EPAELNA_LAA_PIN_COUNT) {
		pr_info("[WARN] '%s' exceeds limit of %d PINs, currently %d",
			CFM_DT_PROP_LAA_PINMUX,
			CONNFEM_EPAELNA_LAA_PIN_COUNT,
			laa_cnt);
		return -EINVAL;
	}

	*laa_cnt_out = laa_cnt;

	return 0;
}

/**
 * cfm_dt_epaelna_flags_parse
 *	Locate & collect the flag nodes for each subsys based on hwid.
 *
 *	Note that for a given hwid:
 *	1. Subsys does not necessary need to define its flags node.
 *		- Subsys' flags np in the flags context will set to NULL.
 *	2. Flags node is defined, but is empty
 *		- Subsys' flags np in the flags context will be valid, caller
 *		  needs to decide how to deal with.
 *
 *	On success, flags context needs to be released via
 *	cfm_dt_epaelna_flags_free() or cfm_dt_epaelna_free();
 *
 * Parameters
 *	np	 : Pointer to the epa_elna/_mtk node containing the subsys node.
 *	hwid	 : Indication on which flag node to be selected.
 *	flags_out: Pointer to the flags context for storing the parsed states.
 *
 * Return value
 *	0	: Success, output parameter will be valid
 *	-EINVAL : Error
 *
 */
static int cfm_dt_epaelna_flags_parse(
		struct device_node *np,
		unsigned int hwid,
		struct cfm_dt_epaelna_flags_context *flags_out)
{
	int i;
	struct device_node *subsys_np;
	struct cfm_dt_epaelna_flags_context flags;

	memset(&flags, 0, sizeof(flags));

	/* Flags node name is based on hwid, it's the same for all subsys */
	snprintf(flags.node_name, sizeof(flags.node_name),
		 "%s%d",
		 CFM_DT_PROP_FLAGS_PREFIX, hwid);

	/* Collect subsys' flags node if valid */
	for (i = 0; i < CONNFEM_SUBSYS_NUM; i++) {
		if (cfm_subsys_name[i] == NULL)
			continue;

		subsys_np = of_get_child_by_name(np, cfm_subsys_name[i]);
		if (!subsys_np) {
			pr_info("[WARN] Skip %s flags, missing '%s' node",
				cfm_subsys_name[i],
				cfm_subsys_name[i]);
			continue;
		}

		flags.np[i] = of_get_child_by_name(subsys_np, flags.node_name);
		if (!flags.np[i]) {
			pr_info("[WARN] Skip %s flags, missing '%s' node",
				cfm_subsys_name[i],
				flags.node_name);
			continue;
		}

		of_node_put(subsys_np);
	}

	memcpy(flags_out, &flags, sizeof(flags));

	return 0;
}
