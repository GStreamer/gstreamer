<?xml version='1.0'?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform"
                version='1.0'>
<xsl:param name="section.autolabel" select="1"/>
<xsl:template match="section[@role = 'notintoc']" mode="toc"/>
</xsl:stylesheet>
